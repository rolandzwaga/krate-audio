<!-- Master plan for the Membrum factory-preset re-tune. Lives at plugins/membrum/PRESET-RETUNE-PLAN.md -->

# Membrum Preset Re-Tune — Master Plan

**Status:** Planning complete (Stage A + Stage B). Ready for implementation.
**Target:** `tools/membrum_preset_generator.cpp` (20 `xxxKit()` builders) → 20 regenerated `.vstpreset` factory kits (state blob v3, 57-slot sound array).
**DSP baseline:** the corrected signal path documented in [`AUDIT-signal-path-2026-06-07.md`](AUDIT-signal-path-2026-06-07.md) (H-1/2/3/4, M-1…M-9, N-1, Plate/Shell/Bell physics, Mode-Stretch B_max=0.01, mode-inject 1/k).

---

## 1. Executive summary

### The problem

Membrum's 20 factory kits were voiced against a **broken** signal chain. The 2026-06-07 audit (§1, §4) traced the "all the presets sound the same / boring" complaint to four root causes, all since fixed **in code** but never reflected **in the presets**:

- gain-staging collapse + a hard clip that buried the modal body under noise/click;
- tom rows built as *one* Membrane recipe size-swept across six pads (`for`-loop stamping);
- cymbal/hat banks built by `pads[8]=pads[6]` clone-then-detune on a NoiseBody where the body knobs barely move the sound;
- the restored variety axes left **unused**: `modeInject` is 0 in 19/20 kits, `decaySkew` flat on most pads, `modeStretch` at default almost everywhere, and **`pan` = 0.5 (center) on every pad in all 20 kits**.

The net effect: a single timbre per family, swept, with the body masked — and a stereo image, an inharmonicity axis, and a per-mode-tilt axis all sitting idle.

### What the re-tune achieves

Every sounding pad in every kit becomes a **physically-grounded, full-parameter voice** — its body/exciter, modal shape, damping, strike position, noise+click layers, tone shaper, pitch envelope, unnatural-zone axes, secondary shell, tension glide, and pan are all set deliberately from a cited acoustic recipe, instead of inherited from a loop default or a sibling clone. This kills the sameness at its source: distinct drums are now distinct in the parameter space, and the corrected DSP stages (body, stretch, skew, inject, stereo) are actually exercised.

### Methodology

A four-stage research workflow, each stage adversarially verified:

1. **Parameter dictionary** — every per-pad param (offsets 0–64 incl. pan) and global, with exact normalized→physical mappings and which body/exciter each is meaningful for. → [`preset-retune/00-param-dictionary.md`](preset-retune/00-param-dictionary.md)
2. **Cited archetype library** — 85 real-drum recipes mapping measured acoustics → exact Membrum values, against the corrected DSP semantics. → [`preset-retune/02-archetype-index.md`](preset-retune/02-archetype-index.md)
3. **Per-kit design** — 20 re-tune plans, one per kit, assigning each pad an archetype and exact per-param normalized values.
4. **Adversarial verify** — each plan re-checked against the dictionary, the archetypes, `current-state.json`, the audit, and the live generator source; defects fixed in place; coverage and ranges confirmed. → [`preset-retune/kits/INDEX.md`](preset-retune/kits/INDEX.md)

### Scope

- **20 kits** (5 each across Acoustic / Electronic / Percussive / Unnatural).
- **85 archetypes** with full exact-value param tables, rationale, and web citations.
- **Exact per-pad normalized values** for every meaningful param of every sounding pad, with every default explicitly justified as a genuine no-op.

All 20 kit plans carry a **pass-with-fixes** verdict. The total of issues fixed during verification is **166** across the library.

---

## 2. How to read this plan

This document is the hub. The detail lives in the linked assets.

**Foundations**

- Parameter dictionary (authoritative mappings): [`preset-retune/00-param-dictionary.md`](preset-retune/00-param-dictionary.md) · machine-readable: `preset-retune/param-dictionary.json`
- Archetype index (85 recipes): [`preset-retune/02-archetype-index.md`](preset-retune/02-archetype-index.md) · per-recipe files: `preset-retune/archetypes/*.md` · machine-readable: `preset-retune/recipes.json`
- Current-state inventory + cross-cutting findings: [`preset-retune/01-current-state.md`](preset-retune/01-current-state.md) · `preset-retune/current-state.json` (incl. `padStructFieldMap`)
- DSP behavior the re-tune targets: [`AUDIT-signal-path-2026-06-07.md`](AUDIT-signal-path-2026-06-07.md)

**The 20 per-kit plans** (each has design + verification log + verdict + coverage status + exact per-pad values):

| # | Kit | Plan | Verdict | Coverage OK |
|---|-----|------|---------|-------------|
| 01 | Acoustic Studio Kit | [`kits/01-acousticKit.md`](preset-retune/kits/01-acousticKit.md) | pass-with-fixes | yes |
| 02 | Jazz Brushes | [`kits/02-jazzBrushesKit.md`](preset-retune/kits/02-jazzBrushesKit.md) | pass-with-fixes | yes |
| 03 | Rock Big Room | [`kits/03-rockBigRoomKit.md`](preset-retune/kits/03-rockBigRoomKit.md) | pass-with-fixes | no |
| 04 | Vintage Wood | [`kits/04-vintageWoodKit.md`](preset-retune/kits/04-vintageWoodKit.md) | pass-with-fixes | yes |
| 05 | Orchestral | [`kits/05-orchestralKit.md`](preset-retune/kits/05-orchestralKit.md) | pass-with-fixes | no |
| 06 | 808 Electronic Kit | [`kits/06-electronicKit.md`](preset-retune/kits/06-electronicKit.md) | pass-with-fixes | no |
| 07 | 909 Drum Machine | [`kits/07-nineOhNineKit.md`](preset-retune/kits/07-nineOhNineKit.md) | pass-with-fixes | no |
| 08 | LinnDrum CR-78 | [`kits/08-linnDrumKit.md`](preset-retune/kits/08-linnDrumKit.md) | pass-with-fixes | no |
| 09 | Modular West Coast | [`kits/09-modularWestCoastKit.md`](preset-retune/kits/09-modularWestCoastKit.md) | pass-with-fixes | no |
| 10 | Trap Modern | [`kits/10-trapModernKit.md`](preset-retune/kits/10-trapModernKit.md) | pass-with-fixes | no |
| 11 | Hand Drums | [`kits/11-handDrumsKit.md`](preset-retune/kits/11-handDrumsKit.md) | pass-with-fixes | yes |
| 12 | Latin Perc | [`kits/12-latinPercKit.md`](preset-retune/kits/12-latinPercKit.md) | pass-with-fixes | no |
| 13 | Tabla | [`kits/13-tablaKit.md`](preset-retune/kits/13-tablaKit.md) | pass-with-fixes | no |
| 14 | World Metal | [`kits/14-worldMetalKit.md`](preset-retune/kits/14-worldMetalKit.md) | pass-with-fixes | yes |
| 15 | Cajon and Frames | [`kits/15-cajonFramesKit.md`](preset-retune/kits/15-cajonFramesKit.md) | pass-with-fixes | yes |
| 16 | Glass Bell Garden | [`kits/16-glassBellGardenKit.md`](preset-retune/kits/16-glassBellGardenKit.md) | pass-with-fixes | yes |
| 17 | Drone and Sustain | [`kits/17-droneSustainKit.md`](preset-retune/kits/17-droneSustainKit.md) | pass-with-fixes | no |
| 18 | Chaos Engine | [`kits/18-chaosEngineKit.md`](preset-retune/kits/18-chaosEngineKit.md) | pass-with-fixes | no |
| 19 | Ghost Bones | [`kits/19-ghostBonesKit.md`](preset-retune/kits/19-ghostBonesKit.md) | pass-with-fixes | yes |
| 20 | Experimental FX Kit | [`kits/20-experimentalKit.md`](preset-retune/kits/20-experimentalKit.md) | pass-with-fixes | no |

> **Reading a `coverageOk: false` header:** it means a *few* params remain at a **documented, justified default**, which the agreed "per pad, where meaningful" policy explicitly allows. It is **not** a failure — see §4.

---

## 3. Global parameter conventions

These are the cross-kit rules the plans follow, with the load-bearing normalization facts pulled from the [param dictionary](preset-retune/00-param-dictionary.md). When implementing, keep values consistent with these — they are how the plans stay coherent across 20 kits.

### Body & exciter selection (offsets 1, 0 — `int` selectors, not normalized)
- Body enum: `0 Membrane / 1 Plate / 2 Shell / 3 String / 4 Bell / 5 NoiseBody`. Exciter enum: `0 Impulse / 1 Mallet / 2 NoiseBurst / 3 Friction / 4 FMImpulse / 5 Feedback`.
- Conventions: **Membrane** = kicks/snares/toms/hand drums/tabla/timpani (only body that consumes `airLoading` + `tensionMod`); **Plate** = free-plate Chladni `(m+2n)^1.7` for cajon tapa, woodblocks, inharmonic/FM toms; **Shell** = free-free bar for side-stick, clave, rim, triangle, feedback-snare; **String** = Karplus-Strong waveguide for friction drones, tubular bells, mbira, tanpura (modal-bank axes are inherent no-ops here); **Bell** = church-bell Chladni for cowbell/agogo/crotale/gong/tingsha/temple/glass/FM-hats; **NoiseBody** = plate-ratio bank + internal noise for hats/cymbals/shakers/claps.

### Size → pitch (offset 3, verbatim)
- `f0 = base · 0.1^size`, base 500 Hz (Membrane) / 800 Hz (Bell) / 1500 Hz (Plate·Shell·NoiseBody). Larger size → lower f0. Tom/bell rows are graded by Size, **not** cloned.

### Material & damping (offsets 2, 50, 51)
- `Material` (2) is body-specific brightness/decay/stretch base. `Body Damping b1` (50) and `b3` (51) carry a **sentinel `-1.0` = "derive"** in `PadConfig`, but the **generator `Pad` struct defaults b1=0.40 and b3=0.40 — NOT neutral**. Therefore any modal pad whose recipe wants metallic highs (b3=0) or a specific decay floor **must set b1/b3 explicitly** (this was a recurring coverage gap: e.g. pandeiro, hand-drum quartet). String pads correctly leave b1/b3 at the sentinel (never written), since writing 0.5 would force audible damping.

### Decay (offset 4): `decayTime *= exp(lerp(ln0.3, ln3, decay))` (10× span); overridden by `b1` when set.

### Strike position (offset 5): mode-shape sampling (Bessel radius / beam / pluck position). A **meaningful, frequently-missed** lever — snares/toms/cymbals/hand-drums set it per recipe; defaulting it to 0.3 was a common silent gap that verification closed.

### Noise + click layers (offsets 42–46 / 47–49)
- Noise: `Mix ·0.243` (0=off), `Cutoff` log[40,18000], `Resonance` 0.3+4.7n, `Decay` log[20,2000]ms, `Color` Brown/Pink/White/Violet. Click: `Mix ·0.445`, `Contact` 2+3n ms, `Brightness` log[200,12000]. Snares/hats lean on these but the re-tune adds shell/body coupling underneath so the body is no longer masked.

### Tone shaper (offsets 7–12): Filter Type `int(n·3)` LP/HP/BP, Cutoff `20·1000^n`, `Drive` is **flavour** (M-2 unity makeup), `Fold` 0..π. Drone/feedback kits use the in-loop bandpass + ADSR as the regenerative band-selector.

### Pitch envelope (offsets 13–16, 60–63)
- Start/End/Mid all use `20·100^n Hz` → **encode with `toLogNorm(hz)=ln(hz/20)/ln(100)`**. `Time` (15) `n·500 ms`, **0 = env OFF (master enable)**. `Curve` (16) `2n−1`; the struct default **0.15** = the legacy exponential drop, deliberately kept. **Pitch-env encoding errors were the single most common defect** (toms decoding 15–25% flat of their Hz target) — re-encode every Start/End/Mid via `toLogNorm`.

### Unnatural-zone axes (offsets 21–24)
- `Mode Stretch` (21): `phys = 0.5 + n·1.5`; **0.333 → 1.0 = physical/no stretch**; B_max now 0.01 (so old high values are far more inharmonic — re-checked on Ghost Bones/Chaos). `Decay Skew` (22): `2n−1`, per-mode tilt, **live on ALL bodies post-M-5**. `Mode Inject` (23): 1/k harmonic injection, exact bypass at 0 — the **free variety axis** the re-tune finally uses (1/k on deep membranes as a syahi/harmonic stand-in, on metallic kits for partial reinforcement). `Nonlinear Coupling` (24): **env-level AM brightener** (M-3/M-4), exact bypass at 0 — used as amplitude bloom on crashes/swells, NOT decaySkew.

### Head-shell coupling + secondary (offsets 54–57)
- `Coupling Strength` (54) head→shell feedforward; `Secondary Enabled` (55) bool; `Secondary Size` (56) `shell f0 = head·(1−0.75n)` (shell can **never** exceed head f0); `Secondary Material` (57). A secondary shell requires **both** `couplingStrength>0` and `secondaryEnabled≥0.5`.

### Tension mod (offset 58): energy pitch glide (~2 st at full vel post-N-1), **MEMBRANE-only** — must be 0 on every non-Membrane pad and on chokes; toms get 0 when it fights a descending boom-glide.

### Air loading (offset 52): Bessel→Rossing depression, **Membrane-effective only** — set only on Membrane pads; a no-op elsewhere.

### Pan (offset 64, M-9): equal-power `gainL=√2·cos(pan·π/2)`, `gainR=√2·sin`. **The restored stereo image — previously 0.5 on all 900+ pads.** Plans spread the field per kit.

### Macros (offsets 37–41): Tightness/Brightness/Body Size/Punch/Complexity, neutral at 0.5; written in separate pad-major blocks (not the 57-slot sound array).

### Choke groups & output bus (offsets 30, 31 — `int`)
- `Choke Group` round(n·8) [0,8], 0=none. `Output Bus` round(n·15) [0,15], 0=main. **Write these as the generator's integer fields directly (`chokeGroup=1`, `outputBus=1`) — do not write the on-wire norm (the `0.0667→bus1` / `0.125→group1` round-trip is the dictionary view, not the struct assignment).**

### Kit globals
- `Global Coupling` (270) must be **>0** for per-pad `couplingAmount` to engage. `Master Gain` (320) `dB=−24+36n`, main bus only. `Max Polyphony` (250) `4+round(12n)`.

---

## 4. Coverage audit

All 20 kits passed (**pass-with-fixes**). `coverageOk:false` flags only that a small number of params remain at a **documented, justified default** — policy-compliant under "per pad, where meaningful." The defaults left are genuine no-ops (wrong-body params, intentional inert axes) or explicitly-reasoned character choices, **not** silent omissions (every silent omission found in verification was fixed).

| # | Kit | Verdict | coverageOk | Issues fixed | Remaining documented defaults (coverageOk:false only) |
|---|-----|---------|:--:|:--:|---|
| 01 | Acoustic Studio | pass-with-fixes | yes | 9 | — (pad 16 GM China = documented spare) |
| 02 | Jazz Brushes | pass-with-fixes | yes | 7 | — |
| 03 | Rock Big Room | pass-with-fixes | no | 8 | ride FM-ratio defaulted (no-op under NoiseBurst); now-fixed strikePos gaps |
| 04 | Vintage Wood | pass-with-fixes | yes | 10 | — |
| 05 | Orchestral | pass-with-fixes | no | 12 | gong secondary OFF, cymbal modeInject 0, bell-tree size 0.10 — all defensible, documented |
| 06 | 808 Electronic | pass-with-fixes | no | 8 | ride on NoiseBody vs archetype Bell (documented 808 delta) |
| 07 | 909 Drum Machine | pass-with-fixes | no | 9 | (gap pads now filled) NoiseBurst duration / hat noise-reso were the false flags, fixed |
| 08 | LinnDrum CR-78 | pass-with-fixes | no | 12 | kick sub-shell + cowbell modeInject kept as documented deltas |
| 09 | Modular West Coast | pass-with-fixes | no | 10 | sub-bell inert feedbackAmount; string-drone inert stretch/skew/damping (no-ops) |
| 10 | Trap Modern | pass-with-fixes | no | 11 | clave woody b3 0.70 vs kept synthetic b3 0 (documented) |
| 11 | Hand Drums | pass-with-fixes | yes | 11 | — |
| 12 | Latin Perc | pass-with-fixes | no | 9 | cuica + clave/castanet b1-default reviewed, documented engine-limit cases |
| 13 | Tabla | pass-with-fixes | no | 5 | (Bell skew/stretch + chimta secondary now set) |
| 14 | World Metal | pass-with-fixes | yes | 10 | — (modeInject 0 / airLoading 0 kit-wide = physically correct, documented per-pad) |
| 15 | Cajon and Frames | pass-with-fixes | yes | 10 | — (frame-drum secondary shell = flagged deliberate divergence) |
| 16 | Glass Bell Garden | pass-with-fixes | yes | 6 | — |
| 17 | Drone and Sustain | pass-with-fixes | no | 7 | temple-bell modeInject 0.22 overrides archetype-off (documented kit-character delta) |
| 18 | Chaos Engine | pass-with-fixes | no | 9 | String waveguide no-ops documented; FM-bell hats re-sized |
| 19 | Ghost Bones | pass-with-fixes | yes | 7 | — (String b1/b3 correctly at sentinel) |
| 20 | Experimental FX | pass-with-fixes | no | 6 | pad 2 strikePos = intentional default (now documented, not silent) |

**Total issues fixed in verification: 166.** Recurring fix classes: pitch-env Hz↔norm re-encoding, silently-defaulted Strike Position, explicit b1/b3 (struct default ≠ neutral), and missing secondary-shell quartets.

---

## 5. Implementation guide

### 5.1 `Pad` struct field ↔ param-offset mapping

The generator's `Pad` struct (`tools/membrum_preset_generator.cpp:77`) is the edit surface. Field → controller-param → `PadParamOffset` is fully enumerated in `current-state.json` → `padStructFieldMap`. Key correspondences:

| Pad-struct field | Offset | Encoding to write |
|---|---|---|
| `exciterType` / `bodyModel` | 0 / 1 | **int enum** (not normalized) |
| `material`,`size`,`decay`,`strikePosition`,`level` | 2–6 | verbatim norm |
| `tsPitchEnvStart/End` | 13/14 | **`toLogNorm(hz)`** |
| `tsPitchEnvTime` | 15 | norm; 0 = env off |
| `modeStretch` | 21 | 0.333 = physical |
| `decaySkew` | 22 | 0.5 = flat |
| `modeInjectAmount`,`nonlinearCoupling` | 23/24 | 0 = exact bypass |
| `chokeGroup`,`outputBus` | 30/31 | **int** (1, not 0.0667) |
| `fmRatio` | 32 | FMImpulse-only |
| `bodyDampingB1/B3` | 50/51 | **explicit if meaningful** (struct default 0.40) |
| `airLoading`,`tensionModAmt` | 52/58 | **Membrane-only** |
| `secondaryEnabled`+`couplingStrength`+`secondarySize/Material` | 55,54,56/57 | set together |
| `pan` | 64 | equal-power |

`couplingAmount` + 5 macros + `chokeGroup`/`outputBus` are written in separate pad-major blocks (not the 57-float sound array) — that wiring already exists in `writeKitBlob` (`tools/membrum_preset_generator.cpp:289`); you only set the struct fields.

### 5.2 Per-`xxxKit()` edit pattern

Each builder starts `Kit k{"Name","Subdir", defaultPads(), {}, {}};` then assigns `pads[i].field = value;` per pad (see `acousticKit()` at line 489 as the canonical example). To implement a kit plan: for each pad in the plan's layout table, replace/add the `pads[i]` assignments to match the plan's exact normalized values, set `k.crafted = {…}` to the plan's contiguous sounding-pad list, and keep `defaultPads()` for the rest (`disableUncraftedPads()` silences them).

### 5.3 Encoding gotchas to respect

- **Selectors are ints**, not norms (`exciterType`, `bodyModel`, `chokeGroup`, `outputBus`).
- **Pitch-env Hz must go through `toLogNorm()`** — never hand-type the norm.
- **b1/b3 struct default is 0.40, not neutral** — set explicitly on modal pads that need metallic/specific damping; leave at sentinel only on String pads.
- **Mode Stretch 0.333 = physical** (not 0); **decaySkew 0.5 = flat**; **modeInject/NLC 0 = exact bypass**.
- **Membrane-only gates:** `airLoading`, `tensionModAmt` are no-ops off Membrane — keep them 0 there for honesty.
- **PitchEnv Time = 0 disables the whole env** regardless of Start/End.

### 5.4 Build / regenerate / verify loop

The state blob is **already v3** (57 slots incl. M-9 pan) — no codec or version change is needed.

1. Build the generator: `cmake --build build/windows-x64-release --config Release --target generate_membrum_presets`
2. Run it to regenerate the 20 `.vstpreset` files (via `writeVstPreset`, line 351). **Presets install to `C:\ProgramData\Krate Audio\Membrum\Kits\<Subdir>\` — copy the regenerated files there manually** (they do not live in the VST3 bundle).
3. `cmake --build … --target membrum_tests` then run; expect the existing 581-case / 91k-assertion suite green.
4. `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3"` (clean / exit 0).
5. Run clang-tidy if any non-preset source changed (preset-data-only edits to the generator may skip it).

### 5.5 Recommended rollout

- **One kit per PR** — small, reviewable diffs; each PR = one `xxxKit()` rewrite + regenerated presets for that kit.
- **Order:** simplest acoustic kits first (Acoustic Studio → Jazz Brushes → Vintage Wood → Rock Big Room → Orchestral), then Percussive, then Electronic, finishing with the Unnatural kits (Chaos Engine, Ghost Bones, Drone) which lean hardest on the corrected stages and most need A/B confirmation.
- **No migration:** the factory presets are **pre-release**; regenerating overwrites them outright. There is no user-state compatibility concern.

---

## 6. Risks & open items

- **Pad-16 GM Chinese-Cymbal spare (Acoustic Studio).** No China archetype is in scope and the overhead bus is already well populated; pad 16 ships as a **documented spare**, not a crafted voice. If a China recipe is later added, it slots here.
- **`coverageOk:false` kits carry intentional documented deltas** (§4) — e.g. Drone temple-bell `modeInject 0.22` overriding the archetype's inject-off, 808 ride on NoiseBody vs archetype Bell, LinnDrum kick sub-shell + cowbell modeInject. These are deliberate kit-character choices, flagged in the plans; reviewers should confirm intent, not treat as bugs.
- **Perceptual-substitution archetypes are approximate by design.** Tubular Bell and Mbira (World Metal) are voiced on the **String** waveguide rather than their physically-exact free-free-bar model, trading the literal inharmonic ladder for a sustaining/buzzy tone. Crotales/kalimba on **Bell** approximate a clamped-free cantilever. These trades are documented in the respective archetype files and should be A/B-confirmed.
- **Unnatural kits re-checked against the widened inharmonicity law.** Ghost Bones and Chaos Engine set high `modeStretch` against the *old* (off-by-one, B_max=0.001) mapping; under the corrected B_max=0.01 they are far more inharmonic, so their values were re-derived — but these are the most likely to need an ear pass.
- **Ground-truth A/B.** Every kit's values are derived analytically against the corrected chain; **once implemented, each kit must be A/B-checked against an actual render** (synthetic test render vs. the regenerated preset) before sign-off. The plans are a strong starting point, not a substitute for listening — especially the feedback/drone/chaos voices that sit near the −1 dBTP limiter.
- **Verification was static.** Defects were caught by reading code/recipes/state, not by rendering audio; a small number of borderline level/headroom choices (hot voices at 0.62–0.80 under the bus limiter) should be confirmed not to pump or clip in a busy mix.

---

*Sources synthesized: `preset-retune/kits/INDEX.md` (verdicts, coverage, 166 fixes), `preset-retune/00-param-dictionary.md` (mappings), `preset-retune/02-archetype-index.md` (85 recipes), `preset-retune/01-current-state.md` + `current-state.json` (`padStructFieldMap`, cross-cutting findings), `tools/membrum_preset_generator.cpp` (`Pad` struct L77, `acousticKit()` L489, `writeKitBlob` L289, `writeVstPreset` L351), `AUDIT-signal-path-2026-06-07.md`.*
