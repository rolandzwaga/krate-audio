# Membrum Recipe — Brush Snare (sweep)

**Body:** Membrane  **Exciter:** NoiseBurst  **Category:** snare

## The sound, physically
A wire brush dragged across a coated snare head. US Patent **4181059A** (the canonical electronic brush-sweep circuit) establishes the defining facts: the sound is **basically unpitched**, arising from **random contact of the brush wires with the head** — i.e. a broadband noise bed, white-noise-derived and bandpass-shaped to **~6.7 kHz and ~17 kHz** (mid/high emphasis). The onset is **deliberately transient-free** — the patent RC-softens the gate to "soften the turn on and turnoff and minimize transients." There is a slow **cyclic loudness variation** (one cycle per 360° brush rotation). Mixing references put the snare "head/swish" band at **6–10 kHz** with air above 8 kHz.

So the **body crack is deliberately absent**: the sound is the *swept noise*, with only a faint, deep, well-damped coated-head body (~180 Hz, air-loaded) warming it underneath. A 14" snare body fundamental is ~170–220 Hz (Madsen Physics-406 measured 211–213 Hz center-strike), but here it is voiced subdued so it never reads as a pitched tom.

## Mapping to Membrum
- **NoiseBurst exciter, long burst (0.85 → ~13 ms):** the longest practical filtered-noise burst gives a soft, smeared onset — no contact crack.
- **Membrane body, deep + air-loaded (size 0.45 → ~178 Hz, airLoading 0.6):** faint coated-head color under the noise; air-loading removes the whistly highs.
- **Click layer OFF (0.0):** the single most important choice — no attack transient, matching the brush's transient-free drag.
- **Noise layer is the star (mix 0.75, violet color, LP ~7.2 kHz, decay ~644 ms):** a bright, long, sustained swept hiss = the brush bed. ToneShaper **Highpass at ~1.26 kHz** rolls the body out of that path so only the bright presence band passes.
- **Strong coupling (0.6):** feeds the global snare-buzz / sympathetic-resonance network so the swept head drives the kit.
- **No drive/fold/morph/pitch-env/mode-inject/nonlinear-coupling:** the noise is clean, unpitched, and static-timbred; body nonlinearity would fight the character.

## Important caveat (brief vs engine)
The brief asks for **frictionPressure 0.60**, but **Friction Pressure only affects the Friction exciter** — under the selected NoiseBurst exciter it is a **no-op**. The intended swept-friction character is instead carried correctly by the **long-decay, high-passed noise layer** (the engine's real brush mechanism, and exactly what Patent 4181059A models). The value is set as requested but flagged inert.

Also note the **Filter Type** field: set **0.34 → Highpass** (idx=int(0.34*3)=1). The brief's "HP-filtered noise" is the ToneShaper HP plus the bright violet noise layer.

## Sources
- US Patent 4181059A — wire-brush snare simulation circuit (unpitched, white-noise → 6.7k/17k bandpass, soft transient-free gate, cyclic envelope)
- Madsen, *Bottom-head Snare's Effect on Snare Drum Acoustics* (Physics 406) — measured fundamental 211–213 Hz
- iDrumTune snare tuning & drum-frequency guides — 14" snare 150–250 Hz; head/swish 6–10 kHz
- MusicGuyMixing snare EQ — wires 2–3 kHz, brightness/air 6–8 kHz+
