# Membrum Recipe — Gong / Tam-Tam (large struck bronze metal)

**Body:** Bell · **Exciter:** Mallet · **Output bus:** aux 1

## Acoustic profile (researched)
- **Pitch:** low, only quasi-definite. A flat tam-tam 'crashes' rather than tunes. Measured 27" Chau ≈ **97 Hz (G2)** with the 2nd partial ≈ **149 Hz (D3)** — a **perfect fifth** that holds near-equal energy long after the mids die; large-gong fundamentals span **~60–140 Hz**.
- **Partials:** **20–50+** strongly **inharmonic** overtones (no integer series) from stiff, non-flat bronze.
- **Bloom / shimmer (the defining feature):** a hard strike makes energy **cascade low→high over up to ~1 s** after impact; the HF modes *don't appear at all* on a soft strike — a **nonlinear, amplitude-dependent** process (Legge & Fletcher, JASA 1989). Modern models reproduce it via the **von Kármán / wave-turbulence** energy cascade (Bilbao, Webb, Wang & Ducceschi, DAFx 2023).
- **Decay:** very long (seconds→minutes); **HF modes decay faster than LF**, so the **low fifth tail** sustains longest.
- **Attack:** soft padded mallet, broad low-dumped contact (struck ~a hand-width off-center for max low end). **Material:** bronze (~76.5% Cu / 22.4% Sn).

## How it maps onto Membrum (corrected post-audit semantics)
- **Bell @ Size 0.85** → f0 ≈ **113 Hz** nominal; hum 0.25·f0 ≈ 28 Hz, prime 0.5·f0 ≈ 57 Hz, quint 0.75·f0 ≈ 85 Hz — the **prime→quint** pair recreates the gong's low fifth and the hum/prime give the subsonic wash.
- **modeStretch ≈ 1.43** + **modeScatter ~7%** bend & smear the discrete Bell lines toward the dense inharmonic tam-tam column.
- **NonlinearCoupling 0.8** (velocity-driven recip-sqrt brightening) + **Material Morph 0.85→0.55 over ~0.4 s** = Membrum's stand-in for the **bloom** (no true von-Kármán cascade exists in the engine).
- **Decay 0.95 + decaySkew +0.30** = longest ring with **fast highs / sustaining lows**.
- **Heavy head↔shell coupling (0.95, secondary on, bright metal shell)** adds the parallel ringing-metal body weight.
- **Low b1/b3** keep the metallic tail long; transparent filter; bright violet noise wash for the sizzle.

## Documented no-ops in the brief
`fmRatio 0.30`, `feedback 0.10`, and `tension` are **exciter/body-gated no-ops** on a Mallet + Bell voice (FM/Feedback = wrong exciter; tensionMod = Membrane-only). They are recorded as defaulted with physical reasons.

> Gain staging assumes post-audit chain: body −6 dBFS budget, bus TruePeakLimiter at −1 dBTP.
