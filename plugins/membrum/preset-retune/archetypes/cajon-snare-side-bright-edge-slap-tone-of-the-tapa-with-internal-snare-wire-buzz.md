# Membrum Recipe — "Cajon Snare-Side" (snare)

**Body:** Plate (free-plate Chladni — the thin plywood *tapa* front panel)  
**Exciter:** NoiseBurst (violet-noise contact burst — hand-slap against the thin plate)  
All values **NORMALIZED [0,1]** (preset / on-wire). Physical target shown for each.

## Physical model
The cajon is a plywood box; the player strikes the thin front plate (*tapa*, ~3 mm birch/maple ply), with a rear sound hole forming a neckless **Helmholtz resonator**. Two zones: a **center "bass" tone** dominated by the cavity resonance (measured ~82-131 Hz; lowest box mode (0,0,1) ~405 Hz), and an **edge "slap/snare" tone** — the subject here. The snare side is produced by internal **snare wires / guitar strings tensioned against the inside of the tapa**: striking the thin plate makes them **buzz/rattle**, adding a sustained bright metallic layer (~3-5 kHz buzz band). The tapa is a thin rectangular plate (inharmonic Kirchhoff modes, NOT a harmonic series), heavily **body-damped** by the seated player, so decays are **short**. There's a brief low **box "thump"** with a fast downward settle behind the bright slap.

This maps onto Membrum exactly like classic analog snare synthesis (TR-808/909, SH-101): a few tonal plate modes + a separately-enveloped **band-limited noise** layer for the wires (A≈0, S=0, short D) + a sharp **slap click**.

## How the archetype is built
- **Plate body @ size 0.27 -> ~430 Hz** = the bright *edge/slap* tone (deliberately above the ~80-130 Hz bass zone).
- **NoiseBurst exciter, ~5 ms** = the hand-on-plate contact slap.
- **Noise layer hot (0.75), violet, ~6 kHz, ~126 ms** = the dominant **snare-wire buzz**.
- **Click hot (0.72), ~5 kHz, 2.5 ms** = the sharp slap crack.
- **modeStretch 1.13 + scatter + nonlinear coupling** = metallic, buzzy, inharmonic plate edge.
- **Coupling 0.62 + secondary shell @ ~252 Hz** = the wooden box/housing behind the tapa.
- **PitchEnv 210->140 Hz / 70 ms** = the short box "kerthump".
- **b1 ~27 s⁻¹, short Decay** = tight, body-damped, hand-played (not a ringing gong).

## Key normalized values
| Param | Norm | Physical |
|---|---|---|
| Exciter Type | 0.40 | NoiseBurst |
| Body Model | 0.20 | Plate |
| Material | 0.50 | plate brightness 0.75 |
| Size | 0.27 | f0 ≈ 430 Hz (slap tone) |
| Decay | 0.34 | short (b1 override sets floor) |
| Strike Position | 0.70 | edge strike |
| Level | 0.85 | linear, pre-limiter |
| Filter Type / Cutoff | 0 / 0.884 | LP @ 9 kHz |
| Mode Stretch | 0.42 | 1.13 (inharmonic) |
| Decay Skew | 0.40 | −0.20 (highs ring longer) |
| Mode Inject | 0.12 | small tonal core |
| Nonlinear Coupling | 0.22 | velocity buzz/brightening |
| Noise Mix / Cutoff / Decay / Color | 0.75 / 0.82 / 0.40 / 0.90 | wires: ~6 kHz, ~126 ms, violet |
| Noise Resonance | 0.15 | gentle (~Q1) |
| Click Mix / Contact / Bright | 0.72 / 0.167 / 0.786 | slap: 2.5 ms, ~5 kHz |
| NoiseBurst Duration | 0.231 | ~5 ms |
| PitchEnv Start/End/Time/Curve | 0.511 / 0.423 / 0.14 / 0.15 | 210->140 Hz, 70 ms, exp |
| Body Damping b1 / b3 | 0.55 / 0.18 | ~27.6 s⁻¹ / bright highs |
| Mode Scatter | 0.30 | ~4.5% dither |
| Coupling Amount | 0.62 | sympathetic buzz weight |
| Coupling Strength | 0.62 | drives secondary shell |
| Secondary Enabled / Size / Material | 1.0 / 0.55 / 0.45 | box @ ~252 Hz, woody |
| Pan | 0.50 | center |

## Defaulted (no-ops / neutral)
Filter envelope (amount 0.5), Drive 0, Fold 0, Morph off, PitchEnv knee off, **Air Loading** (Membrane-only → no-op on Plate), **Tension Mod** (Membrane-only → no-op), FM/Feedback/Friction secondary-exciter params (wrong exciter), Choke 0, Output Bus 0, all five **macros at 0.5** (recipe sets the real params directly), Pad Enabled 1.0.

## Sources
- Kopf Percussion — Anatomy & Physics of Cajon Drums (tapa material, snare wires/strings, edge vs center tone)
- UBC PHYS341 cajon project (Helmholtz resonator, 405 Hz box mode, short body-damped decay, >400 Hz slap tone)
- AIP PoMA *Acoustic and structural resonances of the cajon*; JASA *Resonant mode characteristics of a cajón drum*
- UIUC PHYS406 cajon report (Helmholtz fundamentals 82-131 Hz, port sizing)
- SoundOnSound *Practical Snare Drum Synthesis* (tonal modes + band-limited noise model, A=0/S=0); Sonicbids snare EQ (3-5 kHz wire buzz)
- Membrum AUDIT 2026-06-07 (corrected Plate Chladni, stretch index, mode_inject 1/k, decaySkew per-mode tilt, NonlinearCoupling env-level redesign, N-1 measured-strike norm, linear Level, M-9 pan)