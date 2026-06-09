# Membrum Recipe — "808 Closed Hat" (hat)

## 1. The real instrument (cited research)

The TR-808 closed hi-hat is **100% synthetic** — it is *not* a sampled cymbal. The original circuit:

- **Six square-wave oscillators**, the *same six* shared with the 808 cymbal, tuned to **deliberately inharmonic** frequencies that "avoid even multiples" so the sum is a dense, beating metallic hum with no fused pitch. Measured factory tunings ([Baratatronix circuit analysis](https://www.baratatronix.com/cascadia/cascadia-808-hi-hat)):
  - OSC1 ≈ **800 Hz** (G5+35¢, tuneable 359–1150 Hz)
  - OSC2 ≈ **540 Hz** (C#5−45¢)
  - OSC3 ≈ **523 Hz**, OSC4 ≈ **370 Hz**, OSC5 ≈ **304 Hz**, OSC6 ≈ **205 Hz**
  - Span ≈ **200–800 Hz**, anharmonic.
- **Filtering**: the square sum passes a **bandpass (~3.4 kHz)** in **series** with a **high-pass (~7.1 kHz)** — leaving only the very bright, thin top end. Result is *brighter and more "electronic/buzzy"* than an acoustic hat. ([Baratatronix synthesis blog](https://www.baratatronix.com/blog/cascadia-808-cymbal-hi-hat-synthesis))
- **Envelope**: an envelope-controlled VCA with a **fixed, very short decay (~50 ms)** for the closed hat (service-manual value; closed decay is non-adjustable). ([ModWiggler](https://modwiggler.com/forum/viewtopic.php?t=194942))
- **No contact click / no beater transient** — the attack is just the VCA gating the steady square sum.
- **No pitch glide.** Oscillator frequencies are static (pitch CV only selects closed vs. open by changing the *decay*).

Reference academic model: Werner et al., *"The TR-808 Cymbal: a Physically-Informed, Circuit-Bendable, Digital Model"* ([ResearchGate](https://www.researchgate.net/publication/267630051_The_TR-808_Cymbal_a_Physically-Informed_Circuit-Bendable_Digital_Model)).

## 2. Mapping onto Membrum (post-audit semantics)

- **Body = NoiseBody**: its plate-ratio (inharmonic, metallic) 32-mode bank stands in for the six anharmonic square oscillators, and its always-on **noise layer** carries the bright high-passed sizzle that *defines* the sound.
- **Exciter = NoiseBurst**: a violet-noise bandpass burst is the closest analogue to gating bright filtered noise — no impulse/beater click.
- **Small Size** (f0 ≈ 947 Hz) drops the modal cluster into the 200–800 Hz oscillator band.
- **Very short Decay + high Body Damping b1** realize the fixed ~50 ms VCA; **b3 = 0** keeps it metallic (highs don't roll off).
- **Noise layer dominates** (mix 0.85), **Violet** color, **high cutoff (~11 kHz)**, short decay (~28 ms) → the high-passed treble-tilted spectrum.
- **Mode Scatter 0.35** detunes the modal partials so they beat like the anharmonic squares (which "avoid even multiples").
- **Click Mix = 0** (no transient), **Air Loading = 0** (Membrane-only & irrelevant), **Choke group 1** (mutes against open/pedal hats).
- **No pitch env, no morph, no coupling, no tension, no drive/fold, no mode-inject, no nonlinear-coupling** — the 808 hat is a *static, atonal, clean* filtered-noise burst.

## 3. Baseline (all NORMALIZED [0,1])

| Param | Norm | Physical target |
|---|---|---|
| Exciter Type | 0.40 | NoiseBurst |
| Body Model | 1.00 | NoiseBody |
| Material | 0.92 | brightness ≈0.98; internal-noise cutoff ≈6.1 kHz |
| Size | 0.10 | f0 ≈ 947 Hz |
| Decay | 0.07 | ≈0.32× (near floor) |
| Body Damping b1 | 0.65 | ≈33 s⁻¹ flat damping (short ring) |
| Body Damping b3 | 0.00 | pure flat damping (metallic highs) |
| Noise Mix | 0.85 | dominant noise path |
| Noise Cutoff | 0.92 | ≈11.3 kHz LP corner |
| Noise Color | 0.85 | Violet (+6 dB/oct) |
| Noise Decay | 0.10 | ≈28 ms |
| Noise Resonance | 0.20 | Q ≈1.24 |
| Mode Scatter | 0.35 | ~5% dither (anharmonic beating) |
| Click Mix | 0.00 | no contact click |
| Air Loading | 0.00 | off (and N/A on NoiseBody) |
| Level | 0.75 | ≈−2.5 dB |
| Choke Group | 0.125 | group 1 (open/closed mute) |
| Pan | 0.50 | center |
| Filter Cutoff | 1.00 | 20 kHz → ToneShaper bypassed |
| Pitch Env Time | 0.00 | disabled (no glide) |

All other params left at archetype defaults (see deliberate-defaults list): Strike Position, all ToneShaper filter/env params, Drive, Fold, Mode Stretch, Decay Skew, Mode Inject, Nonlinear Coupling, Morph, all 5 Macros, Coupling/Secondary, Tension Mod, FM/Feedback/Friction secondary-exciter params, Output Bus.

**Note vs. the existing factory `electronicKit()` pad 6**, which is already a strong 808-closed-hat match (material 0.92, size 0.1, decay 0.08, noiseMix 0.85, noiseCutoff 0.92, noiseColor 0.85, noiseDecay 0.10, click 0, airLoading 0, choke 1). This recipe matches that and adds the explicit **Body Damping b1=0.65 / b3=0** and **Mode Scatter 0.35** to tighten the ring and de-harmonize the metallic cluster.