# Membrum Recipe — "909 Snare" (snare)

## What the real thing is (researched)
The TR-909 snare is a **dual-layer design**: two decaying **tone oscillators** for the dominant drumhead (0,1-region) modes — roughly **~180 Hz + ~330 Hz**, the lower one given a slightly **longer decay** — sitting over a **broadband noise** layer that emulates the snare wires. The 909's noise generator is a **digital LFSR pseudo-noise** (31-stage shift register, taps 31/13, ~300 kHz clock → spectrally **flat well past audibility**), which is exactly why the 909 snare is **brighter and hissier with a longer, more "sampled" tail** than the 808's softer analog/bandpassed noise. The noise is lowpass-then-highpass shaped with its own envelope; **SNAPPY** trades noise-vs-tone and attack tightness, **TONE** is the noise lowpass cutoff. Attack is near-instantaneous (A=0) with a sharp stick crack. The snare's audible pitch drop is captured by the task's **380 Hz → 140 Hz** glide.

Sources: McGill Nord percussion text (two triangle oscillators, lower=longer decay; white noise via LP→HP), SOS "Practical Snare Drum Synthesis" (~180/330 Hz two modes, A=0, longer noise tail), Roland Cloud 909/808 design article (909 = brighter/more metallic, less of the 808's soft noise), Electric Druid (909 LFSR digital noise = flat/bright), firstpr 909 service mods (snare tuning).

## Membrum mapping
- **Exciter:** NoiseBurst (broadband noisy contact, seeds the head modes).
- **Body:** Membrane primary (head "boop", f0 ~200 Hz) **+ secondary metallic shell** (Coupling 0.32 / Secondary Material 0.90, Size 0.32) = the 909's bright **second tone oscillator**.
- **Pitch glide:** 380 → 140 Hz over 25 ms, fast exponential drop.
- **Noise layer (the 909 signature):** Mix 0.72, **Violet** color, cutoff 0.78 (~5 kHz), **Decay 0.48 (~200 ms long bright tail)** — brighter & longer than the body.
- **Drive 0.48:** fattens body harmonics as TIMBRE (post-M-2 makeup, no level pump).
- **Click 0.55 @ ~2.3 ms, 4.7 kHz:** the stick crack.
- **Pan center**, full Level (bus limiter owns the ceiling per the corrected gain staging).

All values below are **normalized [0,1]** (preset/on-wire). Key denorms: pitchEnvStart 0.6394→380 Hz, end 0.4226→140 Hz, Size 0.398→~200 Hz Membrane f0, Noise cutoff 0.78→~5 kHz, Noise decay 0.48→~200 ms, Secondary Size 0.32→0.76×head f0.

## Post-audit notes
- Voiced against the **corrected** chain: linear voice + measured-strike body norm + −1 dBTP bus limiter, so Level=1.0 is safe and Drive is flavour (M-2). Pitch-env now drives Membrane correctly (H-3). Per-pad pan engaged (M-9). NonlinearCoupling deliberately left OFF (its redesign is velocity-brightening, which the static 909 snare doesn't want).
- The metallic ping uses the **secondary shell** path, not Mode Inject — matching the literal two-oscillator 909 topology and keeping the spectrum clean.