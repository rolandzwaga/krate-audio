# Membrum Recipe — Clave

**Body model:** Shell (free-free Euler-Bernoulli bar) · **Exciter:** Impulse

## 1. The real instrument (researched acoustic profile)

A clave is a pair of **short (20–25 cm), thick (~2.5 cm) SOLID hardwood cylinders** — rosewood, ebony, grenadilla, or hard maple. One is balanced on the fingertips over a **cupped hand that forms a small resonating cavity**; the other strikes it. The result is a *bright, penetrating, very dry "tok"*.

Acoustically the clave is a **struck free-free wooden bar** — the same physics class as an un-undercut xylophone bar, NOT a membrane, plate, string, or bell:

- **Inharmonic free-free bar partials:** ratio series **1 : 2.756 : 5.404 : 8.933 …** (xylophone/marimba bar physics; Rossing / Euphonics). Non-integer → the characteristic woody, slightly clangy-but-dry ring.
- **Pitch / brightness:** dominant ringing partial roughly **~1200–2500 Hz** (the short stiff bar's fundamental, reinforced by the **~1200–1500 Hz cupped-hand cavity formant**); the 2.756× partial adds a bright "ting" around **3.5–4.5 kHz**. Claves are very high-pitched and cut through a full ensemble.
- **Decay:** **very short, ~100–250 ms.** Dense hardwood has strong frequency-squared internal damping; the upper inharmonic partials die even faster than the fundamental.
- **Attack:** a **sharp wooden-contact click** — two hard sticks meeting, contact time only a few ms.
- **Noise:** **essentially zero** — a clean, near-deterministic idiophone (no hiss/air/sizzle).
- **Material:** hard, stiff, **bright wood** — woody, not metallic (partials ring but damp quickly).

## 2. Mapping onto Membrum (corrected post-audit semantics)

**Why Shell + Impulse:** Shell is Membrum's free-free Euler-Bernoulli bar (ratios 1, 2.757, 5.404, 8.933…), the only body matching the measured clave/xylophone-bar spectrum. The post-audit Shell now uses the true free-free eigenfunction (antinodes at the free ends), so a near-end strike excites the full inharmonic set. Impulse is the ideal hard wood-on-wood contact.

**Pitch:** Shell base f0 = 1500·0.1^Size. **Size = 0.0 → f0 = 1500 Hz**, putting the tok in the clave's ringing region with the 2.757× partial at ~4.1 kHz.

**Decay & material:** Material 0.85 = bright stiff hardwood. Because Shell brightness *rises* with material, the material-derived high-mode damping (b3) would go near-zero (long, metallic highs) — wrong for a dry clave. So **b3 is OVERRIDDEN to 0.70** (≈0.7e-3 s; b3·f² ≈ 1575 s⁻¹ at the fundamental, ~12000 s⁻¹ at the 4 kHz partial): the fundamental rings briefly while the upper partials die fast. A low **Decay = 0.12** keeps the audible ring ~120–180 ms.

**Attack click:** Click layer raised (Mix 0.55, Contact 2.45 ms, Brightness center ~5.9 kHz) for the signature sharp wooden tick.

**No noise, no pitch glide, no saturation, no coupling, no injected harmonics** — everything that would make it dirty, pitched-tonal, or sustained is left off so the pure inharmonic wooden tok is exposed.

## 3. Parameter table (exact normalized values)

| Param | Norm | Denorm / target | Why |
|---|---|---|---|
| Body Model | 0.40 | **Shell** (free-free bar) | Matches measured clave 1:2.76:5.40 free-free partials |
| Exciter Type | 0.00 | **Impulse** | Ideal hard wood-on-wood click |
| Material | 0.85 | brightness ~0.98, base decay 0.70 s | Hard bright stiff hardwood |
| Size | 0.00 | f0 = **1500 Hz** (2.757× ~4136 Hz) | Small, very high-pitched tok |
| Decay | 0.12 | flat-damping baseline ~0.30 s; ring ~120–180 ms after b3 | Very short, dry ring |
| Body Damping b3 | 0.70 | 0.7e-3 s; b3·f² ~1575 s⁻¹ @1500 Hz, ~12000 @4 kHz | Strong wood HF damping (override material) |
| Strike Position | 0.12 | near free-end antinode | Excites full bright inharmonic set |
| Click Mix | 0.55 | contact transient on | Signature sharp wooden tick |
| Click Contact | 0.15 | 2.45 ms | Extremely brief hard contact |
| Click Brightness | 0.82 | ~5900 Hz bandpass center | High, snappy attack |
| Noise Mix | 0.00 | layer bypassed | Clave has ~zero noise content |
| Mode Inject | 0.00 | bypassed | Keep spectrum purely inharmonic |
| Drive Amount | 0.00 | bypassed | Clean, undistorted |
| Fold Amount | 0.00 | bypassed | No added aggressive harmonics |
| Filter Cutoff | 1.00 | 20 kHz = filter bypass | Natural brightness, no post EQ |
| Air Loading | 0.00 | no-op on Shell | Membrane-only correction |
| Mode Stretch | 0.333 | physical 1.0 (neutral) | Preserve authentic free-free ratios |
| Level | 0.80 | linear 0.8 | Loud, penetrating element |
| Pan | 0.50 | center | Mono element |

## 4. Deliberately left at default

- **Body Damping b1** — sentinel; ring length controlled via Decay (b3 dominates HF decay).
- **Decay Skew (0.5), Mode Scatter (0)** — free-free bar's natural decay balance + clean repeatable spectrum need no tilt/dither.
- **Nonlinear Coupling (0)** — a struck wooden bar is linear at normal dynamics.
- **PitchEnv Time = 0** (+ all pitch-env sub-params) — a clave has NO pitch glide; static-pitch tok.
- **All ToneShaper filter sub-params** — filter bypassed (cutoff 1.0), so inert.
- **Morph (all)** — material/timbre does not sweep within a hit.
- **FM Ratio / Feedback / NoiseBurst Duration / Friction Pressure** — no-ops (exciter is Impulse).
- **Noise Cutoff/Reso/Decay/Color** — no-ops (Noise Mix = 0).
- **Coupling / Secondary resonator (all)** — solid clave, no two-body head-shell coupling; cavity approximated by f0/brightness.
- **Tension Mod** — Membrane-only; no clave pitch glide.
- **Macros (Tightness/Brightness/Body Size/Punch/Complexity = 0.5)** — neutral so explicit per-param values stand.
- **Choke Group / Output Bus / Pad Enabled** — kit-routing defaults.

## 5. Sources

- Claves — Wikipedia; Britannica (dimensions, materials, bright penetrating click, cupped-hand resonator)
- Euphonics 3.3 / ResearchGate — physics of xylophone & marimba bars (free-free bar ratios 1 : 2.76 : 5.40 : 8.90)
- CCRMA Percussion notes — struck-bar modal physics
- StockMusicMusician / Mixing&Mastering EQ charts — claves need little EQ, minimal low end, no noise, presence 2–5 kHz
