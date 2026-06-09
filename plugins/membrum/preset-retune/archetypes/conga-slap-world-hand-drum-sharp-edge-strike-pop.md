# Membrum Recipe — "Conga Slap" (world hand drum)

**Body:** Membrane (Bessel circular drumhead, 48 modes)  ·  **Exciter:** Impulse (raised-cosine click)
GM map: pad 4 in the Hand Drums / Percussive kit.

> The sharp cracking "pop" of a hand slapping the conga's rim — a high-frequency edge transient with almost no body sustain, the antithesis of the ringing open tone.

## Acoustic basis (cited)
- **Fundamental ~200 Hz**, measured harmonics near **400 Hz and 700 Hz** → ratios ≈ 1 : 2 : 3.5, i.e. **inharmonic** (circular-membrane Bessel zeros, e.g. (2,0)/(1,0) ≈ 2.295). Radiated range ~100 Hz–12 kHz. [audiorecording.me], [hmc timpani], [congaplace freq]
- **Slap = edge strike** (r/a ≈ 0.9): suppresses the central (0,1) head mode, emphasises high circumferential modes → bright "pop". [congarix strokes]
- **Slap presence/pop ≈ 5 kHz**, presence band 2–5 kHz, "smack" ~1–2 kHz. [audiorecording.me]
- **Very short body tail** — the slap is choked by the hand (defining contrast vs the open tone). [congarix strokes]
- Material: tensioned skin on hardwood/fiberglass shell → **woody, not metallic**. [Wikipedia conga]

## Membrum mapping (all values NORMALIZED [0,1])

| Param | Norm | Physical target | Why |
|---|---|---|---|
| Exciter Type | 0.00 | Impulse | Hard rigid hand crack = wideband impulse |
| Body Model | 0.00 | Membrane | Conga = circular membrane, inharmonic Bessel modes |
| Material | 0.55 | woody-bright | Skin-on-wood, slap reads brighter than open tone |
| Size | 0.50 | f0 ≈ 158 Hz | Conga body register (edge strike pushes centroid up) |
| Decay | 0.18 | ~0.4× base | Choked, short body tail |
| Strike Position | 0.10 | r/a ≈ 0.09 (edge) | Edge strike → high-mode bright slap |
| Level | 0.85 | linear 0.85 | Slap is the loudest, cutting stroke |
| Filter Cutoff | 1.00 | 20 kHz (bypass) | Brightness from strike+click, not a filter |
| Filter Env Amt | 0.50 | 0 (no sweep) | Clean acoustic slap |
| Drive / Fold | 0.00 / 0.00 | bypass | No saturation on a natural drum |
| PitchEnv Time | 0.00 | env DISABLED | No 808 glide on a choked slap |
| Mode Stretch | 0.333 | 1.0 (physical) | Bessel ratios already inharmonic |
| Decay Skew | 0.50 | 0 (neutral) | Short Decay does the work |
| Mode Inject | 0.00 | bypass | Don't add a tonal harmonic series to a transient |
| Nonlinear Coupling | 0.00 | bypass | Crack already maximally bright |
| Noise Mix | 0.15 | faint bright "pap" | Small skin-edge noise (< snare) |
| Noise Cutoff | 0.70 | ~4.5 kHz LP | Sits noise in the 2–5 kHz slap band |
| Noise Decay | 0.18 | ~41 ms | Quick edge noise |
| Noise Color | 0.78 | White | Bright presence-band content |
| **Click Mix** | **0.85** | dominant transient | **Slap identity = the contact crack** |
| Click Contact | 0.10 | ~2.3 ms | Near-instant slap contact |
| Click Brightness | 0.85 | ~6.4 kHz center | Centers the crack on the ~5 kHz pop |
| Body Damping b1 | 0.45 | ~22.6 s⁻¹ | Tight decay floor → dry choke |
| Body Damping b3 | 0.10 | 1e-4 s | Keep edge highs bright through the short tail |
| Air Loading | 0.40 | mild deepening | Conga head air-loads; modest below 0.6 default |
| **Mode Scatter** | **0.30** | ~4.5% dither | Push ideal ratios toward measured 1:2:3.5 inharmonicity |
| Coupling Strength | 0.20 | shell ×0.20 | Faint wood-shell housing ring |
| Secondary Enabled | 1.00 | shell bank ON | Wood-shell housing resonance |
| Secondary Size | 0.40 | shell f0 ≈ 0.70·head | Housing just under head pitch |
| Secondary Material | 0.40 | damped wood | Wood shell, short ring |
| Tension Mod | 0.00 | OFF | Choked slap shows no open-tone kerthump |
| **Macro Punch** | **0.85** | shorter attack/contact | Extra-sharp slap crack (per brief) |
| Pan | 0.42 | slightly left | Off-center kit placement (mix-safe) |
| Pad Enabled | 1.00 | ON | — |

All other params left at archetype defaults (filter ADSR, morph, FM/Feedback/Friction/NoiseBurst secondary params, pitch-env knee/mid, choke, bus, remaining macros) — see the defaulted-params list; each is inert because its gating stage (filter env, morph, the non-selected exciters, the disabled pitch env, global coupling) is off.

### Builder mapping (tools/membrum_preset_generator.cpp, Pad struct — pad 4 of handDrumsKit)
```cpp
pads[4].exciterType = ExciterType::Impulse;
pads[4].bodyModel   = BodyModelType::Membrane;
pads[4].material = 0.55; pads[4].size = 0.50; pads[4].decay = 0.18;
pads[4].level = 0.85; pads[4].strikePosition = 0.10;
pads[4].airLoading = 0.40; pads[4].modeScatter = 0.30;
pads[4].couplingStrength = 0.20; pads[4].secondaryEnabled = 1.0;
pads[4].secondarySize = 0.40; pads[4].secondaryMaterial = 0.40;
pads[4].clickLayerMix = 0.85; pads[4].clickLayerContactMs = 0.10;
pads[4].clickLayerBrightness = 0.85;
pads[4].noiseLayerMix = 0.15; pads[4].noiseLayerCutoff = 0.70;
pads[4].noiseLayerDecay = 0.18; pads[4].noiseLayerColor = 0.78;
pads[4].bodyDampingB1 = 0.45; pads[4].bodyDampingB3 = 0.10;
pads[4].macroPunch = 0.85;
pads[4].pan = 0.42;
```
This matches and lightly refines the existing pad-4 voicing (adds explicit noise band-targeting at the 2–5 kHz slap presence and a gentle pan), all consistent with the post-audit corrected semantics (linear gain-staging, measured-strike body norm, per-pad pan M-9, click-free retrigger M-8).
