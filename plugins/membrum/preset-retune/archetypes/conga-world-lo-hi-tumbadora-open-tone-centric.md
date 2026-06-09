# Membrum Recipe — Conga (world), open-tone-centric (lo & hi)

**Body:** Membrane (Bessel circular drumhead, 48 modes) · **Exciter:** Impulse + Click layer
**Voiced as a tuned pair:** lo conga open tone ~200 Hz, hi conga ~280 Hz — the same instrument tuned apart.

## Physical model
A conga is a single-headed Afro-Cuban hand drum: a rawhide (or synthetic) head, screw-tensioned over a staved **wooden barrel shell**, open at the bottom. The open tone (struck with four fingers near the rim) excites the low membrane modes; the position weighting captures finger placement.

- **Pitch:** open-tone fundamental ~200 Hz (lo) / ~280 Hz (hi), harmonics near 400/700 Hz. Family is tuned to 4th/5th intervals. *(Wikipedia Conga; uspto Tuning-of-a-drum patent; artdrum tuning FAQ; audiorecording.me.)*
- **Modes:** 2-D circular membrane → **inharmonic Bessel** ratios (1, 1.594, 2.136, 2.296…), dense. Shell + enclosed air load the lowest modes toward a more pitched 1:~1.5:~2 (Rossing air-loading). Single-headed/open-bottom ⇒ **moderate** air loading. *(Fletcher & Rossing; Wikipedia circular membrane; Rossing 1982.)*
- **Decay:** focused low-mid ring a few hundred ms; high modes die fastest (b3·f²). Not sustained — hand-damped. *(Chaigne & Askenfelt 1994.)*
- **Attack:** woody mid-frequency finger-contact tick (Click layer). Slap would be brighter/shorter.
- **Noise:** minimal — rawhide is warm/low-overtone; only a trace of dark skin-contact noise. *(Remo/Rhythm Notes head guides.)*
- **Glide:** small velocity-dependent "kerthump" (tension modulation) — short pitch env + Membrane tensionModAmt. *(Avanzini-Marogna-Bank JASA 2012; Tolonen-Välimäki-Karjalainen 2000.)*
- **Body:** wooden barrel modeled via the passive **secondary (shell)** resonator at moderate head→shell coupling.

## Parameters (NORMALIZED [0,1])
| Param | Norm | Physical target | Why |
|---|---|---|---|
| Exciter Type | 0.0 | Impulse | finger strike, clean impulse into head |
| Body Model | 0.0 | Membrane (48-mode Bessel) | a conga IS a circular membrane |
| Material | 0.45 | warm-woody rawhide | rawhide fundamental-dominant, warm |
| Size | 0.62 / 0.50 | natural f0 ~120 / ~158 Hz (lo/hi) | low hand-drum register; pitch fixed by env |
| Decay | 0.40 | ~1× base | rings but hand-damped, not boomy |
| Strike Position | 0.40 | r/a=0.36 (mid, near-rim open tone) | open tone four fingers near rim |
| Level | 0.80 | linear | per-pad loudness |
| Filter Cutoff | 1.0 | 20 kHz = bypassed | membrane+damping shape the tone |
| Filter Env Amount | 0.5 | 0 (no mod) | no filter sweep |
| Drive / Fold | 0.0 / 0.0 | bypassed | acoustic, no saturation |
| PitchEnv Start | 0.50 / 0.560 | ~200 / ~280 Hz | open-tone fundamental start |
| PitchEnv End | 0.438 / 0.503 | ~150 / ~210 Hz | tension settle pitch |
| PitchEnv Time | 0.04 | ~20 ms | short 'kerthump' settle |
| PitchEnv Curve | 0.15 | fast initial drop | tension relaxes fast then settles |
| Mode Stretch | 0.333 | 1.0 physical | keep physical Bessel ratios |
| Decay Skew | 0.5 | neutral | b3 already tilts energy low |
| Mode Inject | 0.0 | bypassed | inharmonic membrane, not a harmonic stack |
| Nonlinear Coupling | 0.0 | bypassed | glide handled by tension mod |
| Noise Mix | 0.10 | low dark contact noise | trace of skin/air contact |
| Noise Cutoff / Res / Decay / Color | 0.40 / 0.2 / 0.20 / 0.40 | ~440 Hz LP, Q1.2, ~80 ms, Pink | dark, brief, under the body |
| Click Mix | 0.50 | woody contact tick | signature hand-drum attack |
| Click Contact | 0.20 | ~2.6 ms | soft finger (hand) contact |
| Click Brightness | 0.55 | ~1.8 kHz bandpass | woody mid tick, not a stick click |
| Body Damping b1 | 0.30 | ~15 s^-1 (~0.3-0.4 s) | controlled hand-drum ring |
| Body Damping b3 | 0.10 | 1e-4 (moderate f²) | kills metallic highs → woody |
| Air Loading | 0.50 | 50% toward air-loaded series | moderate (single-headed, open bottom) |
| Mode Scatter | 0.10 | ~1.5% dither | rawhide organic unevenness |
| Coupling Strength | 0.30 | head→shell drive | woody barrel after-ring |
| Secondary Enabled | 1.0 | shell bank on | wooden barrel body |
| Secondary Size | 0.45 | ~0.66·head_f0 | barrel below head (~0.6 rule) |
| Secondary Material | 0.40 | woody short shell | wood, dies before head tone |
| Tension Mod | 0.20 | ~+0.4 st at full vel, relaxing | velocity 'kerthump' (Membrane-only) |
| Pan | 0.5 | center | baseline (spread lo/hi at kit assembly) |

## Articulation notes
- **Slap tone:** strikePosition → ~0.10 (edge), decay ↓ (~0.18), Click Mix ↑ (~0.85) + Click Brightness ↑ (~0.85), Click Contact ↓, drop the pitch env, Macro Punch ↑.
- **Bass tone:** strikePosition → ~0.50 (center), Material ↓, lower pitch.
- **Lo/hi pair:** identical voice, Size/PitchEnv tuned a 4th–5th apart; pan modestly L/R when assembled into a kit.

## Defaulted (per-pad coverage policy)
ToneShaper filter/env (bypassed — modes+damping shape the tone), Drive/Fold (acoustic), Mode Inject (inharmonic membrane), Nonlinear Coupling (glide via tension mod), PitchEnv Knee/Mid/Mid Frac/Curve2 (single short segment), Material Morph (static within a hit), FM Ratio / Feedback / NoiseBurst / Friction (wrong exciter — no-ops), inter-pad Coupling Amount + all Macros (neutral 0.5, kit/performance level), Choke/Bus/Enabled (routing).

## Sources
Wikipedia Conga · Wikipedia Quinto · Wikipedia Vibrations of a circular membrane · artdrum tuning & sizing FAQs · uspto Tuning-of-a-drum patent · audiorecording.me conga EQ · Remo conga drumhead guide · Rhythm Notes conga heads · (acoustics) Fletcher & Rossing *Physics of Musical Instruments*; Rossing 1982 air loading; Chaigne & Askenfelt 1994; Avanzini-Marogna-Bank JASA 2012; Tolonen-Välimäki-Karjalainen 2000.