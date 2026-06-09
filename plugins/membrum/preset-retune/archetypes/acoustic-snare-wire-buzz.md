# Membrum Recipe — Acoustic Snare (wire-buzz)

**Body:** Membrane (Bessel circular head, 48 modes) · **Exciter:** NoiseBurst
**One-line:** A two-headed 14" snare — a short, heavily-damped ~200 Hz batter head coupled to a resonant bottom head/shell, with the snare wires supplying a loud bright broadband buzz and a sharp 5 kHz stick crack on top.

## Acoustic basis (cited)

| Property | Real snare | Source |
|---|---|---|
| Fundamental | ~180–200 Hz (parallel (0,1) two-head mode); 14" tunes well ~170–200 Hz | Rossing & Bork JASA 1992; Madsen Phys406; iDrumTune |
| Mode structure | Enharmonic; each (0,1)/(1,1) mode SPLITS into a pair via air coupling | Rossing & Bork 1992 |
| Decay | Snares-on loses ~64% of peak by 80 ms; (0,1) head pair >2× faster than other partials | Madsen Phys406; SoS |
| Crack/attack | Sharp stick crack 3–5 kHz; wires snap back against head | iDrumTune; SoS |
| Wire buzz | Broadband, tilted up (sizzle/rattle 5–7 kHz+), slight tail past body | iDrumTune; Kiive; izotope |
| Pitch glide | Only a tiny upward tension blip on attack, then relax — NOT a synth boom | Avanzini et al. JASA 2012 |

## Mapping rationale

- **Membrane + NoiseBurst**: a snare is a struck circular membrane with a noisy stick/wire contact — only the membrane mapper offers Bessel inharmonic modes, air-loading and the Membrane-only tension glide.
- **Size 0.40 → 199 Hz** places the body on the measured (0,1) fundamental. **Mode Scatter 0.28** reproduces the (0,1)/(1,1) mode splitting; **Mode Stretch left neutral** because Bessel ratios are already the correct inharmonic set (no Mode Inject — an integer series would make it pitched/tom-like).
- **Decay 0.28 + b1 override 30 s⁻¹** make the head a short tat (snares-on damping); the **noise layer (mix 0.82, violet, ~10.9 kHz LP, ~290 ms)** carries the audible "shhh" tail — this is the wire buzz and most of the sound.
- **Click layer (mix 0.92, ~2.5 ms, ~6.6 kHz)** is the stick crack; it also half-feeds the body to excite high modes.
- **Filter env +0.44 / 65 ms** brightens the attack then closes — the SoS velocity-driven LP sweep. **Nonlinear Coupling 0.22** makes hard hits crackier (velocity-sensitive).
- **Secondary shell on, coupling 0.78** models the resonant bottom (snare-side) head + 14"×5" shell the batter couples to (air-enclosure coupling).
- **PitchEnv 200→130 Hz over 35 ms, fast curve** + **Tension Mod 0.16** = the head's brief detension/settle — deliberately short and shallow so it never reads as a synth kick.

See the `params` array for the exact normalized baseline and physical target of every meaningful parameter, and `defaultedParams` for what is intentionally left at default and why.