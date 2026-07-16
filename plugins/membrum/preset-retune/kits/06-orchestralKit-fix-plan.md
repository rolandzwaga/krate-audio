# Orchestral Kit — Diagnosis & Fix Plan (v2)

**Scope:** `orchestralKit()` in `f:/projects/iterum/tools/membrum_preset_generator.cpp` (lines 2524–2816), plan-of-record `plugins/membrum/preset-retune/kits/05-orchestralKit.md`, plus tooling (`tools/membrum-fit/src/refinement/render_voice.cpp`) and three small DSP items. All findings below are CONFIRMED or MODIFIED by adversarial verification; REJECTED items are in the Appendix.

---

## 1. Executive summary

The kit fails for four compounding, measured reasons. **(1)** Seven pads (timpani 0/5/7/9/11/14 at modeInject 0.10–0.12, gong 13 at 0.15) violate the hard constraint that `modeInject > 0` rings as an undamped flat 1/k plateau — on the timpani row this plateau sits at roughly −19 to −23 dBFS and outlasts the drum, which *is* the "bass note" the user hears; it was invisible in every measurement because the fit harness never forwards modeInject (`render_voice.cpp:56`). **(2)** The snare shipped without the `wireCoupling`/`noiseLayerGain` retrofit every sibling snare received (acoustic :587/590, rock :2059-60, vintage :2341-42), so the wire buzz is gain-capped at −18 dBFS, ~12 dB under the −6 dBFS body — measured 2–8 kHz energy fraction 0.0017 with woodblock modal peaks at 689/539/373/234 Hz. **(3)** Every metallic pad is strangled by preset-layer damping/wash errors: the struck cymbal's decay 0.50 sits exactly on the washBlend dead zone (`noise_body_mapper.h:174`, sizzle 57 ms vs a 948 Hz modal ping; in-plugin b1 0.45 → 22.6 s⁻¹ → a 0.3 s pitched tonk), the ride's 16-line Bell ladder rings 8–13 s against a 726 ms wash, the gong has literally 0.0 measured energy in 2–8 kHz behind a pink-not-violet 1.15 kHz-LP'd noise layer, and crotales/triangle b1 0.30 (15.1 s⁻¹, T60 0.46 s) chokes what should be a 3–8 s ring while triangle b3 0.02 kills its 6.7 kHz partial in ~7 ms. **(4)** The GM mapping puts a Tubular Bell on MIDI 39 ("Clap" — hardcoded UI label, `pad_grid_view.cpp:74`), crotales-lo on 55 ("Splash"), and a Triangle on 48 (Hi-Mid Tom), so GM content and the UI both promise sounds the pads don't make. Additionally, timpani strike position 0.28 puts the pitch-carrying (1,1) mode −6.6 dB under the pitchless (0,1) (Bessel: +6.5 dB predicted at r/a 0.25), and airLoading 0.80 leaves the (m,1) series 37–74 cents flat of Rossing's 1.5/2/2.44/2.9. Most measurements were taken through a harness that silently drops noise/click layers, bodyDamping overrides, airLoading, modeInject, morph, and wireCoupling — fixing that harness is a prerequisite for verifying anything else.

## 2. Complaint → pad map

| User complaint | Pad(s) / MIDI | Measured symptom | Root-cause layer |
|---|---|---|---|
| "Snare not a snare" | pad 2 / MIDI 38 | 2–8 kHz energy fraction **0.0017**; modal peaks 689/539/373/234 Hz (hollow woodblock); wire buzz capped −18 dBFS vs −6 dBFS body | **Preset** (missed wireCoupling/noiseLayerGain rollout, commit e7802363 postdates kit plan) + **harness** (noise layer rendered at mix 0 — *verify in-plugin first*) |
| "'Clap' nothing like a clap" | pad 3 / MIDI 39 | Tubular Bell (String/Mallet chime) on the GM Clap slot; UI hardcodes label "Clap" (`pad_grid_view.cpp:74`) | **Kit design / GM mapping + UI labels** (no ClapExciter pad despite `clap_exciter.h` existing) |
| "Crash/splash/ride read as a tone" | pads 6/8/10 (42/44/46), 13 (49), 15 (51), 19 (55) | Pad 6: single 948 Hz ping, sizzle 57 ms, in-plugin T60 ~0.3 s tonk. Ride 15: bare 16-line Bell ladder t60 8–13 s vs 726 ms wash. Gong 13: 4 low partials, **0.0** energy 2–8 kHz, undamped 113 Hz inject plateau. Pad 19: crotales chime on the Splash slot | **Preset** (washBlend dead zone at decay 0.50; b1 chokes; LP/pink noise beds; modeInject 0.15) + **body-model structure** (Bell = 16 quasi-harmonic lines) + **mapping** (55 = crotales-lo) |
| "Timpani toms read as a bass note" | pads 0, 5, 7, 9, 11, 14 (36, 41/43/45/47/50) | Undamped 1/k plateau ~−19..−23 dBFS riding/outlasting the tail; (1,1) pitch mode −6.6 dB under pitchless (0,1); (m,1) partials 37–74 cents flat of 1.5/2/2.44/2.9 | **Preset** (modeInject 0.10–0.12 — hard-constraint violation; strikePosition 0.28/0.30 center strike; airLoading 0.80) — plateau invisible in renders because of the **harness** gap |

## 3. Measured evidence table

Features from `wav-features.js` (`centroidHz`, `flatnessHigh` [2–10 kHz], `dominantPeaks`, `t60s`, `tailFloorDb`, `attackMs`) on membrum-fit renders. ⚠ = number is corrupted by the harness fidelity gap (§5-D1) — *verify in-plugin / post-harness-fix*.

| Pad | MIDI | Instrument | Key measured features | Verdict |
|---|---|---|---|---|
| 0 | 36 | Timpani low | Partial ratios 1.56–1.59 / 2.22–2.27 / 2.79–2.88 (in-vacuo Bessel ⚠ — airLoading never applied); t60 ~4.4–4.6 s ⚠ (production b1 path → 0.65–1.33 s); (1,1) −6.6 dB vs (0,1); "fundamental" 72.3 Hz is a pitch-env sweep smear ⚠ | FAIL |
| 5,7,9,11,14 | 41–50 | Timpani toms ×5 | Same Bessel-ladder/undertone signature as pad 0; modeInject 0.10–0.12 plateau not rendered ⚠ (constraint violation in-plugin) | FAIL |
| 2 | 38 | Orchestral snare | 2–8 kHz fraction 0.0017 ⚠ (noise layer at mix 0 in harness; in-plugin still gain-capped −18 dBFS); peaks 689/539/373/234 Hz; t60 1.31 s ⚠ (production b1 0.62 → 0.22 s) | FAIL |
| 4 | 40 | Gran cassa | Render t60 2.56 s on harness (derived-damping) path ⚠ — but in-plugin b1 0.30 → 15.14 s⁻¹ → **0.46 s** (kit-kick length, not a concert bass bloom); 59.1 Hz "fundamental" is sweep smear ⚠ | FAIL (in-plugin) |
| 6 | 42 | Susp. cymbal struck | Dominant peak ~948 Hz (predicted 946.5 = NoiseBody f0 at size 0.20); internal sizzle 57 ms (washBlend = 0 at decay 0.50); harness modal T60 3.07 s ⚠ vs in-plugin ~0.3 s (b1 0.45 → 22.6 s⁻¹) | FAIL |
| 8 | 44 | Susp. cymbal pedal | Sizzle 27 ms; in-plugin T60 ~0.2 s muted bell (b1 0.65 → 32.6 s⁻¹) | FAIL |
| 10 | 46 | Susp. cymbal roll | flatnessHigh 0.765 (good wash); elevated tailFloor = 3 s wash still decaying at end of 4 s render (artifact of render length); morph swell never rendered ⚠ | PASS (re-render ≥6 s post-changes) |
| 12 | 48 | Triangle | >2 kHz deficit is a harness artifact (clickLayerMix 0.95 never forwarded) ⚠; in-plugin b1 0.30 → 0.46 s ring, b3 0.02 → **7 ms** T60 at the ~6.7 kHz partial | FAIL (in-plugin) + mapping FAIL (GM tom fills ding a triangle) |
| 13 | 49 | Gong/Tam-tam | 4 low partials only; **0.0** energy fraction 2–8 kHz; 97/149 Hz prime–quint pair; Bell top partial 12×113 = 1356 Hz; noise bed Pink through 1.15 kHz LP; modeInject 0.15 plateau (in-plugin) ⚠ | FAIL |
| 15 | 51 | Ride cymbal | t60 8–13 s ⚠ (harness drops b1 0.16) bare 16-line ladder; noise wash 726 ms (0.78 → 726 ms exact); shipping per-pad test shows silent by 8 s | FAIL |
| 17 | 53 | Crotales hi | Dominant "fundamental" 151.7 Hz = hum (0.25×) of the 800 Hz-capped f0 606.9 Hz; t60s = 999 flag, tail ~−24 dB at 3 s (legacy hum T60 18.7 s ⚠) | FAIL |
| 19 | 55 | Crotales lo | Same: 120.5 Hz hum of capped 482.1 Hz; also occupies the GM Splash slot | FAIL |
| 1 | 37 | Bell tree | Top peaks 159.1/319.1/384.6/484.2 Hz = 0.25/0.50/0.60/0.75 of nominal 635.5 Hz — hum-dominant (decaySkew sign inverted: hum ×2.174, 12× partial ×0.249, 18.8 dB spread the wrong way); violet sizzle LP'd at 632 Hz (cutoff unset → 0.5) | FAIL |
| 3 | 39 | Tubular bell | Sound itself coherent; defect is the role: chime on the Clap slot with a hardcoded "Clap" UI label | FAIL (mapping only) |

## 4. Per-pad fixes (preset/generator layer)

All values normalized [0,1] unless noted. "arch" = instrument archetype doc; "verify" = adversarial verification pipeline; "audit" = kit-level audit.

### 4.1 Timpani row — pads 0, 5, 7, 9, 11, 14 (generator :2528–2651)

| Param | Current | Proposed | Rationale | Source |
|---|---|---|---|---|
| `modeInjectAmount` (all six) | 0.12 (pad 0, :2545); 0.10–0.12 (`timpaniInject[]` :2621, :2638) | **0.0** | Hard-constraint violation: `mode_inject.h:118-124` has no envelope — undamped flat plateau at ~−19..−23 dBFS outlasting the drum (then hard cut at the ~1.4 s force-retire), the exact "bass note" signature. Archetype line 50: "Mode Inject 0: timpani are clean & linear". Identical regression already fixed on the crash pad (:1879). Macro system verified NOT to re-add it on load (`processor.cpp:1007-1009` syncs cache from cfg). | verify F0 CONFIRMED; supersedes kit-doc deviation #7 |
| `strikePosition` (all six) | 0.28 (pad 0, :2534); unset → 0.3 default (toms) | **0.83** | Mapper measures from CENTER (`membrane_mapper.h:112-118`, r/a = strikePos×0.9). At r/a 0.25, \|J₀\|=0.910 vs \|J₁\|=0.429 → pitchless (0,1) +6.5 dB over pitch-carrying (1,1); measured −6.6 dB (1,1) deficit. At 0.83 (r/a 0.747) the Bessel weights balance ≈0.33/0.37/0.39/0.39 — Rossing edge-strike pattern. Archetype line 27's "0.28 \| edge strike" contradicts its own line 13; fix the archetype table too. | verify F1 CONFIRMED |
| `airLoading` (all six) | 0.80 (:2540, :2636) | **1.00** | `kAirLoadingTargetScale` lerp lands the (m,1) series 37–74 cents flat of 1.50/2.00/2.44/2.90 at 0.80; only 1.0 hits the table exactly. Frequency-only on 4 modes — the documented "over-thinning" rationale has no code mechanism (no amplitude/damping counterpart in `membrane_modes.h`). Archetype line 33: "**Air Loading 1.00** … the defining trick". | verify F2 CONFIRMED (overrides kit-doc deviation #4, rationale refuted from code) |
| `noiseLayerColor` (toms 5/7/9/11/14) | unset → 0.5 default | **0.12** (Brown) | Same-instrument mandate: pad 0 uses 0.12/0.20; toms fell to struct defaults — omission, not voicing. | audit (medium confidence) |
| `noiseLayerDecay` (toms) | unset → 0.3 default | **0.20** | Ditto. | audit |

*Not done here:* per-mode (0,n) radiation-damping — deferred DSP item D8; re-measure the row after the harness fix first (residual (0,1) is only ~15% longer T60 than (1,1) once the strike fix lands).

### 4.2 Orchestral snare — pad 2 (:2555–2588)

| Param | Current | Proposed | Rationale | Source |
|---|---|---|---|---|
| `noiseLayerGain` | unset → 1.0 default | **6.2** (raw F64 blob field, NOT a valueNorm — legal, identical to shipping siblings) | Legacy buzz calibrated −18 dBFS (`noise_layer.h:312-321`), ~12 dB under the −6 dBFS body; `pad_config.h:255-259` documents the exact "hollow woodblock" failure. Siblings all ship 6.2 (:587, :2059, :2341). | verify F0 CONFIRMED; audit |
| `wireCoupling` | unset → 0.0 default | **0.45** | Modal-energy-coupled buzz (tracks/chokes with the body); rollout (e7802363) postdates the kit plan — omission, not deviation. Clamped in `drum_voice.h:1151`. | verify F0 CONFIRMED |
| `strikePosition` | unset → 0.3 default | **0.35** | Never assigned in the pad block; archetype says 0.35; kit plan silent → archetype governs. | verify F3 MODIFIED |

*Verify in-plugin first:* the 0.0017 HF fraction came from a render with the noise layer at mix 0. Kept as-is (documented/approved): secondary shell (deviation, see Appendix), material 0.60, size 0.33, noiseLayerColor 0.90.

### 4.3 Gran cassa — pad 4 (:2590–2610)

| Param | Current | Proposed | Rationale | Source |
|---|---|---|---|---|
| `bodyDampingB1` | 0.30 (:2608) | **0.05** | 0.30 → b1 15.14 s⁻¹ → T60 0.46 s (kit-kick, not concert bass). 0.05 → 2.69 s⁻¹ → T60 ~2.57 s, matching the passing harness render (2.56 s) and the derived-path value (~2.50 s⁻¹) the archetype mandates ("b1 derived from Decay", line 49). Silent drift in the kit doc (line 46, not in its divergence list) — update that line to 0.05 in the same change. Leave unset is not an option (−1 sentinel can't survive the param system). | verify F0 CONFIRMED |
| `strikePosition` | unset → 0.3 default | **0.35** | Archetype line 30 (r/a ≈ 0.31 off-center); kit plan silent → archetype governs. | verify F5 MODIFIED |

Kept: level 0.88 (kit plan's deliberate post-audit restoration), noiseLayerColor 0.12 (house Brown; 0.10 is a same-band no-op), airLoading 0.80 (matches its own archetype line 32), pitch env 110→40 Hz (archetype's documented boom-glide).

### 4.4 Suspended cymbals — pads 6 & 8 (:2670–2687)

| Param (pad 6) | Current | Proposed | Rationale | Source |
|---|---|---|---|---|
| `decay` | 0.50 | **0.85** | washBlend = clamp((decay−0.5)×2) = 0 today → 57 ms sizzle; 0.85 → blend 0.7 ties wash to modal T60 (`noise_body_mapper.h:170-176`). Struck sus cymbal rings 3–8 s (arch. line 60). | verify F0 MODIFIED |
| `size` | 0.20 | **0.35** | f0 946.5 → ~670 Hz, off the measured 948 Hz ping; surviving modes ~24 → ~27 of 64 at 48 kHz (stretch-aware Nyquist cull). | verify F0 |
| `strikePosition` | unset → 0.3 | **0.85** | Archetype line 19 edge strike; excites m-rich plate families. | verify F0 |
| `modeStretch` | 0.55 | **0.70** | Archetype line 22; B 0.003 → 0.0049 (~27% spread at n=64; costs ~2–3 top modes, acceptable under the longer wash). | verify F0 (corrected math) |
| `modeScatter` | 0.40 | **0.60** | Archetype line 36 (~9% dither). Plugin path only until harness wired. | verify F0 |
| `tsFilterType` | unset → LP 0.0 | **HP (0.5)** | Archetype line 21; decodes HP in both plugin and harness. | verify F0 |
| `tsFilterCutoff` | unset → 1.0 bypass | **0.62** | 20×1000^0.62 ≈ 1.45 kHz HP attenuates the fundamental ping. | verify F0 |
| `material` | 0.90 | **0.95** | Archetype line 16; internal noise cutoff 6000→6250 Hz, brightness 0.97→0.985. | verify F0 |
| `bodyDampingB1` | 0.45 | **0.05** | 0.45 → 22.6 s⁻¹ = the in-plugin 0.3 s tonk. Finding's 0.18 still caps T60 at ~0.75 s (denorm b1 = 0.2+49.8n); 0.05 → 2.69 s⁻¹ → low-mode T60 ~2.6 s, wash ~1.0 s — actual sus-cymbal ring. (Archetype's own 0.18 table value belongs to its closed-hat column and contradicts its 3–8 s physics text.) | verify F0 MODIFIED (corrected value) |
| `noiseLayerColor` | 0.70 (White band) | **0.85** | Crosses ≥0.80 → Violet — real categorical change (arch. line 32). | verify F0 |
| `noiseLayerDecay` | 0.12 (~35 ms) | **0.85** (~1.0 s) | 20×100^n ms; 35 ms is inaudible under a multi-second ring. Plugin path (harness bypasses layer). | verify F0 |
| `nonlinearCoupling` | 0.25 | **0.35** | Archetype line 24 energy-cascade brightening. | verify F0 |
| `level` | 0.70 | **0.62** | Archetype line 20; headroom for the longer denser tail. | verify F0 |

| Param (pad 8, inherits pad 6 via :2685) | Current | Proposed | Rationale |
|---|---|---|---|
| `decay` | 0.18 | **0.12** | Pedal chick stays below the wash gate by design (archetype pedal decay 0.10). |
| `bodyDampingB1` | 0.65 | **0.30** | 0.65 → 32.6 s⁻¹ (0.2 s muted bell); 0.30 → 15.1 s⁻¹ (T60 ~0.46 s metallic chick, near archetype 0.22). |
| `noiseLayerMix` | 0.65 (inherited) | **0.75** | Choked body is correctly wash-free; chick character must come from the parallel violet layer. |
| `noiseLayerDecay` | 0.07 | **0.10** (~32 ms) | Archetype pedal class ~25 ms; tight but audible. |

### 4.5 Suspended cymbal roll — pad 10 (:2689–2705) — currently PASSES; re-render after

| Param | Current | Proposed | Rationale | Source |
|---|---|---|---|---|
| `modeStretch` | unset → 0.333333 (= phys 1.0, B 0.0011 — NOT zero stretch) | **0.70** | Archetype line 22; B → 0.0049. Known trade: Nyquist cull drops ~41 → ~29 modes at 48 kHz — acceptable, wash dominates; confirm by re-render. | verify F3 MODIFIED (corrected rationale) |
| `strikePosition` | unset → 0.3 | **0.90** | Archetype line 19 (rolled edge contact). | verify F3 |

### 4.6 Gong / Tam-tam — pad 13 (:2707–2730)

| Param | Current | Proposed | Rationale | Source |
|---|---|---|---|---|
| `modeInjectAmount` | 0.15 (:2715) | **0.0** | Hard constraint. No envelope in `mode_inject.h`; undamped 113 Hz harmonic plateau rides the whole multi-second tail — musically backwards on an inharmonic instrument. Absent from the gong archetype; the kit plan's own verification item 10 ("modeInject avoided on indefinite-pitch metallics") contradicts its pad-13 table. Sibling kits explicitly zero inject "keep inharmonic" (:5995, :6027, :6315). | verify F0 CONFIRMED; audit |
| `bodyModel` | Bell | **Plate** | 48 dense inharmonic Chladni modes (to ratio 29.4) vs Bell's 16 quasi-harmonic lines (top partial 12×113 = 1356 Hz → measured 0.0 energy 2–8 kHz). Design revision — document in kit doc. | verify F3 MODIFIED |
| `size` | 0.85 | **0.92** | Plate has no sub-fundamental partials (Bell's hum/prime made the sub-100 Hz wash). 0.92 → Plate f0 ≈ 96 Hz, inside the archetype's 60–140 Hz gong band, near the measured 97 Hz Chau reference. | verify F3 MODIFIED |
| `noiseLayerCutoff` | 0.55 (~1.15 kHz LP) | **0.85** (~7 kHz) | Archetype mandates a bright wash; 0.55 chokes the pad's only HF source. | verify F3 |
| `noiseLayerColor` | 0.45 (decodes **Pink**) | **0.90** (Violet, ≥0.80 threshold) | Archetype line 18: "bright violet noise wash for the sizzle". | verify F3 |
| `morphDuration` | 0.85 (~1.7 s) | **0.20** (10+0.20×1990 ≈ 408 ms) | Archetype's ~0.4 s low→high bloom; 1.7 s was an undocumented plan-level divergence (cited cascade physics tops out ~1 s). This corrects the plan, not generator drift — log it in the kit doc. | verify F4 MODIFIED |

Kept: secondary OFF (documented deviation — Appendix), nonlinearCoupling 0.80, couplingAmount 0.95 (watchlist, §6).

### 4.7 Ride cymbal — pad 15 (:2732–2748)

| Param | Current | Proposed | Rationale | Source |
|---|---|---|---|---|
| `decay` | 0.90 | **0.62** | 8+ s bare 16-line ladder outliving a 726 ms wash is the "tone". Archetype's own source (SoS): ~3.7 s shimmer vs ~0.2 s ping — the preset inverts that balance. Do NOT go below ~2 s (Musical-U: ping partials hold past 2 s). ⚠ mapping to seconds is unverified — *re-verify post-harness-fix before locking*. | verify F2 CONFIRMED |
| `noiseLayerDecay` | 0.78 (726 ms exact; archetype's "~880 ms" annotation is wrong) | **1.00** (2000 ms ceiling) | Shimmer wash must approach the ring length; 2 s is the layer's hard ceiling (`denormDecayMs`) — see DSP item D6 for beyond. | verify F2 |
| `decaySkew` | 0.62 | **0.70** (phys +0.4) | Lifts upper-partial ring per the archetype's own reading. | verify F2 |
| `noiseLayerMix` | 0.45 | *hold at 0.45* | Finding 2's mix lift was not carried with a concrete value in the verification record; raise only if the post-harness-fix A/B still shows a wash deficit. | verify F2 (caveat) |

This becomes a NEW documented deviation from the ride archetype (kit doc currently claims "matches the archetype exactly") — record it in the 05-orchestralKit.md divergence list.

### 4.8 Triangle — pad 12 (:2653–2668)

| Param | Current | Proposed | Rationale | Source |
|---|---|---|---|---|
| `bodyDampingB1` | 0.30 (:2665) | **0.02** | 0.30 → 15.1 s⁻¹ → T60 0.46 s; 0.02 → 1.196 s⁻¹ → T60 ~5.78 s — a triangle rings for seconds. (Exact constant is taste; direction/magnitude verified.) | verify F4 CONFIRMED |
| `bodyDampingB3` | 0.02 (→ 2e-5 s → ~925 s⁻¹ at the ~6.7 kHz partial → T60 **7 ms**) | **0.0** | The HF shimmer *is* the triangle; 0.02 murders it instantly. 0.0 = flat floor like the sibling metals (pads 1/6/15/17 all ship b3 0.0). Confirm shimmer by render. | verify F4 |
| `level` | 0.65 | **0.72** | Below its own archetype (0.72), inside the old clip-dodging band the plan's thesis condemns — clearest pre-retune leftover. | audit |

⚠ Harness renders the triangle with the click layer silenced (mix 0.95 never forwarded) — the measured >2 kHz deficit was an artifact; *verify in-plugin / post-harness-fix*.

### 4.9 Crotales — pads 17 & 19 (:2750–2769)

| Param (both pads; 19 copies 17) | Current | Proposed | Rationale | Source |
|---|---|---|---|---|
| `strikePosition` | 0.10 | **0.50** | θ = strikePos×π/2 (`bell_modes.h:95`): at 0.10 the m=2 hum factor is 0.951 (near max) — hence measured "fundamentals" 151.7/120.5 Hz = 0.25× hum of the 800 Hz-capped f0. At 0.5 hum AND prime hit cos(π/2)=0 → 0.05 clamp (−26 dB) while nominal (m=4) is full. Fall back to ~0.45 if the 3.2× partial (also clamped) proves audible in A/B. | verify F3 CONFIRMED |
| `decaySkew` | 0.58 (phys +0.16: hum ×1.25, 12× ×0.67 — wrong direction) | **0.35** (phys −0.30: hum ×0.66, 4× ×1.52) | Energy belongs in the audible octave/upper partials ~1.2–1.4 kHz per the archetype's own identity statement; within the ×8 clamp. | verify F3 |
| `tsFilterType` | unset → LP 0.0 | **HP (0.5)** | ToneShaper sits after body+noise+click sum → genuinely removes the hum. | verify F3 |
| `tsFilterCutoff` | unset → 1.0 bypass | **0.53** (20×1000^0.53 = 779 Hz) | Above both capped hums (151.7/120.5 Hz), below the nominals (606.9/482.1 Hz)… no — 779 Hz sits above the nominals too by design of the finding: it keeps only the octave/upper structure that reads as "crotale". | verify F3 |
| `bodyDampingB1` | 0.30 (:2761, inherited by 19) | **0.04** | 0.30 → T60 0.456 s; archetype's own decay-realism section demands 3–8 s principal ring (its "0.30 \| 15.1 s⁻¹" table row is internally inconsistent with its own text — not re-litigation). 0.04 → 2.19 s⁻¹ → T60 ~3.15 s. | verify F4 CONFIRMED |

The true fix for the 151.7/120.5 Hz register problem is the Bell 800 Hz f0 cap (DSP item D5, L-effort, own spec); the above is the sanctioned near-term voicing path (archetype: "Keep Size small; do not widen it chasing pitch").

### 4.10 Bell tree — pad 1 (:2771–2787)

| Param | Current | Proposed | Rationale | Source |
|---|---|---|---|---|
| `decaySkew` | 0.78 (phys +0.56: hum ×2.174, 12× ×0.249 — **sign inverted**, 18.8 dB tilted toward the hum; measured top peaks 159.1/319.1/384.6/484.2 Hz = 0.25/0.50/0.60/0.75 of nominal 635.5 Hz) | **0.22** (phys −0.56: hum ×0.46, 4× ×2.17, 12× ×4.02, inside ×8 clamp) | Canonical semantics triple-verified (`bell_mapper.h:76-85`, `membrane_mapper.h:154-156`, `default_kit.h:231`): NEGATIVE skew lifts upper partials. The archetype doc itself carries the inverted sign (bell-tree-bell.md:16,:33) and propagated it — fix the archetype doc in the same pass (also correct crotales archetype :39 wording). | verify F7 CONFIRMED |
| `bodyDampingB1` | 0.35 (:2778) | **0.05** | 0.35 → 17.6 s⁻¹ → T60 ~0.39 s; 0.05 → 2.69 s⁻¹ → T60 ~2.57 s shimmer bed. | verify F4 CONFIRMED |
| `noiseLayerCutoff` | unset → 0.5 (**632 Hz LP** on the violet sizzle — worst single drift) | **0.92** (~13 kHz) | Archetype: sizzle must reach the top octaves. | verify F6 MODIFIED; audit |
| `noiseLayerMix` | 0.15 | **0.42** | Archetype value. | verify F6 |
| `noiseLayerDecay` | 0.40 | **0.85** (~1.1 s sizzle tail) | Archetype value. | verify F6 |
| `clickLayerMix` | 0.30 | **0.55** | Archetype value. | verify F6 |
| `clickLayerBrightness` | 0.85 | **0.92** | Archetype value. | verify F6 |
| `modeScatter` | 0.35 | **0.55** | Archetype ~8% microtonal detune. | verify F6 |
| `modeStretch` | 0.45 (phys 1.175, B 0.002 — already stretches; NOT a dead axis) | **0.55** (B 0.003) | Archetype fidelity; mild change. | verify F6 (corrected rationale) |
| `strikePosition` | 0.15 | **0.30** | Archetype; m=2 hum factor 0.89 → 0.59. | verify F6 |
| `level` | unset → 0.8 default | **0.70** | Archetype; generator never sets it. | verify F6; audit |

Keep `size` 0.10 (documented deviation #8). **Also update the pad-1 spec in 05-orchestralKit.md** — the drift originates in the kit plan, not generator transcription; fixing only the generator leaves the plan-of-record wrong and the retune pipeline will re-propagate it.

### 4.11 Tubular bell — pad 3

No voicing changes; role/mapping change only (§6).

## 5. DSP work items

| # | Item | Evidence | Effort | Unblocks |
|---|---|---|---|---|
| **D1** | **Fit-harness fidelity** — `tools/membrum-fit/src/refinement/render_voice.cpp` `applyPadConfig` (:25-57) forwards only core/ToneShaper/stretch/skew. Mirror `voice_pool.cpp:785-887` **wholesale** (with its denorm conventions): noiseLayer\* (incl. gain/resonance), clickLayer\*, bodyDampingB1/B3 (honor −1 sentinels), airLoading, modeInject, nonlinearCoupling, wireCoupling, modeScatter, secondary shell, tensionModAmt, noiseBurstContactMs, FM/feedback/friction, and the ENTIRE material-morph block. Extract a shared apply-helper so plugin and harness cannot diverge again. Extend sus-cymbal/gong/ride render length to ≥6 s. | Confirmed independently by six pipelines (timpani F4, snare F2, bass F1, sus F1/F2, ride F5, small-metals F5/F9); ClickLayer/NoiseLayer mixes default 0.0 → renders had ZERO click and ZERO noise; damping sentinel path rings ~8× longer than plugin | **M** | Verification of *every* pad; findings 4.1–4.10 cannot be validated without it |
| **D2** | **ModeInject decay envelope** — one-pole multiplier in `mode_inject.h` (preserves the amount==0 exact bypass; RT-safe). Defuses the macro re-arm hazard: from a loaded 0.85 brightness cache, nudging to 1.0 adds (1.0−0.85)×0.30 = 0.045 of *undamped* injection even after presets zero the field (`macro_mapper.h` spans 0.30). | verify ride-gong F6 CONFIRMED. Note: changes the sound of other kits' documented inject use — re-audition those pads as part of the change | **M** | Makes modeInject usable as a feature again, kit-wide; permanently closes the plateau class of bug |
| **D3** | **NoteOn-only factory-kit idle-decay test** — `test_kit_switch_infinite_ring.cpp`: the NoteOn-only scenario (:184) covers only test-local kits; the factory sweep (:627, :795) sends paired NoteOn/NoteOff. Voice auto-release threshold is −60 dBFS (`voice_pool.cpp:348`); inject plateaus sit at ~−19..−23 dBFS → factory kits with modeInject>0 ring forever on note-on-only hosts, untested. | audit (high confidence); factory-wide gap (many kits ship 0.10–0.30) | **M** | Guards the constraint for all kits; prerequisite for ever re-enabling inject post-D2 |
| **D4** | **Mallet contact-time hint from body size** — `mallet_exciter.h:63` hardcodes mass 0.3 → base contact T 9.46 ms (3.76 ms @v=1); an 8.2 ms Hann-like pulse has its spectral null region near ~244 Hz — wrong exciter for small metals (need ~0.5–2 ms). Pass a contact/mass hint derived from body size from DrumVoice into MalletExciter; **do NOT velocity-map mass** (tried and reverted per `mallet_exciter.h:53-56` — breaks the 10 ms centroid window / velocity-ratio tests, US1-2). Optional: ~20 Hz HP on ImpactExciter output — shared Layer-2 KrateDSP, land as its own commit with `dsp_processors_tests` + kick/timpani golden re-checks. | verify small-metals F2 MODIFIED | **M** | Crotales 17/19, triangle 12, bell tree 1, gong 13 attack character (timpani/gran-cassa keep the long soft contact) |
| **D5** | **Bell register law** — `bell_mapper.h:38` caps f0Nominal at 800 Hz (f0 = 800×0.1^size); with kBellRatios[0]=0.25 the measured crotale "fundamentals" are hums at 151.7/120.5 Hz. Real crotales sit ≥1.5 kHz. Changing the law touches every Bell pad in every kit + goldens → **own gated/audited spec**. | verify small-metals F1 CONFIRMED; archetype itself documents the cap | **L** | True crotale/bell-tree register; until then §4.9 voicing is the sanctioned path |
| **D6** | **Ride structural gap** — tuned-ping Bell body = 16-line spectrum (`bell_modes.h`, Phase-8B "mode count stays at 16"); NoiseLayer decay hard-capped 2000 ms; no single body gives ping + 3.7 s shimmer. Options: NoiseBody rebuild at size 0.22 (f0 ≈ 904 Hz) — sacrifices the archetype's defining bell-cup ping, needs design signoff — or raise the noise-decay ceiling (preset-migration hazard: decay-curve remap must be gated). §4.7 is the interim. | verify ride-gong F1 CONFIRMED | **L** | Pad 15 beyond the preset rebalance |
| **D7** | **Bell-tree cascade exciter** (optional design gap) — engine is structurally single-onset (one exciter → one bank, `drum_voice.h:520-524`; morph re-maps material only); a bell tree is a 300–800 ms gliss across 14–28 bells. ClapExciter (4 bandpassed bursts ~10 ms apart) is prior art / natural template for a rising-cutoff burst train. "Accept single shimmer strike + §4.10 preset fixes" is a legitimate outcome. | verify small-metals F8 CONFIRMED | **M/L** | Pad 1 realism ceiling |
| **D8** | **Kettledrum (0,n) radiation damping — DEFERRED.** Diagnosis confirmed (damping law is global b1+b3·f², monotone in f → lowest mode always rings longest; no per-mode override exists), but the proposed airLoading-keyed amplitude hack is rejected: airLoading 0.6–0.8 ships on membrane pads across ALL kits → would cut kick/tom fundamentals kit-wide 36–48%. Re-measure the row post-D1 with §4.1 applied; implement per-mode decay-rate multiplier (backward-compatible, default 1.0, driven by a NEW mapper term) ONLY if a residual (0,1) undertone is still measurable, gated on A/B renders of every Membrane pad in every kit. | verify timpani F3 MODIFIED | **M** (if triggered) | Timpani residual undertone (~15% longer (0,1) T60 post-fix — likely acceptable) |
| **D9** | **Doc/comment bug** — `modal_resonator_bank.h:96` says "decayTime is the t60" but :103 returns b1 = 1/decayTime → true T60 = ln(1000)×decayTime = 6.9×. Fix the comment now; the behavior-level ln(1000) reconciliation shifts every sentinel-path preset → own audited change. | verify small-metals F5 CONFIRMED | **S** (comment) / L (reconciliation) | Prevents the next mis-tuned decay |
| **D10** | **Stored pad display names** — preset format stores no names; `pad_grid_view.cpp:68-107` renders fixed GM labels, so every non-GM voicing in every kit is mislabeled (state-codec version bump + PadGridView change). Own spec. | audit (high confidence) | **L** | Truthful UI for all kits; the only complete fix for "'clap' isn't a clap" labeling |

## 6. Kit-level changes

- **maxPolyphony:** keep generator's **16** (:2808) — legal, in [4,16]. **Fix the plan doc** (05-orchestralKit.md says 20 — a hard-constraint violation on paper; note "20 capped to 16").
- **Coupling:** keep `globalCoupling 0.28 / snareBuzz 0.20 / tomResonance 0.35 / couplingDelayMs 1.6` — squarely in the acoustic-kit band (siblings 0.18–0.30 / 0.10–0.35 / 0.20–0.45 / 0.8–2.0 ms). **Watchlist:** gong pad 13 `couplingAmount 0.95` + decay 0.95 + bloom — every loud hit feeds a near-max-coupled gong; trim to ~0.7 only if users report wash buildup, verified with an **in-plugin** render (the fit harness excludes the coupling engine entirely).
- **Levels:** triangle 0.65 → **0.72** (§4.8), bell tree default 0.8 → **0.70** (§4.10), sus-cymbal 0.70 → **0.62** (§4.4). Leave ride/crotales 0.72 (match archetypes) and sus 0.72 on pad 10. Record the metallic levels in the plan doc so they stop looking unspecified.
- **GM mapping — decision: hybrid remap (generator-only), keeping the orchestral identity in the fixed Acoustic category.** Rationale: the user's complaint is about the SOUND under GM content and the hardcoded UI labels; full GM purity would gut the kit (no clap/splash exists in an orchestra), and pad-name storage (D10) is a separate L spec.
  1. **MIDI 48 (highest priority):** insert a **6th timpani tuning** at pad 12 continuing the row between pads 11 (350 Hz) and 14 (440 Hz), so GM tom fills run clean across 41/43/45/47/48/50. **Triangle relocates** to a currently-disabled slot off the GM-critical path — pad 18 (MIDI 54, Tambourine label).
  2. **MIDI 39:** move **Tubular Bell** (pad 3) to an unused high slot, e.g. pad 23 (MIDI 59). On 39, either leave empty or add a short dry percussive stand-in (temple-block/castanet-ish Shell/Impulse — castanets-on-39 is orchestral GS/XG tradition); spec the choice + values in the 05-orchestralKit.md update, and note `ClapExciter` exists if a true clap is ever wanted.
  3. **MIDI 55:** move **crotales-lo** off the Splash slot to pad 16 (MIDI 52), adjacent to crotales-hi (17); place a **splash-role voice at 55** — a copy of the fixed pad 6 with `decay 0.25, size 0.15`.
  4. **Keep:** the documented 36-timpani/40-gran-cassa swap, the 42/44/46 suspended-cymbal set, 49 gong, 51 ride — role-compatible under GM content and load-bearing for the kit's identity.
  Update `k.crafted` (:2814) and the plan doc's pad table to match.

  > **IMPLEMENTED (phase b, 2026-07-16).** Final layout: 6th timpani at pad 12 (MIDI 48;
  > hi 395 → lo 190 Hz, size 0.64, decay 0.54, b1 0.19, skew 0.54, pan 0.545 — interpolated
  > between the 350 and 440 Hz tunings); triangle → pad 18 (MIDI 54); crotales-lo → pad 16
  > (MIDI 52); splash-role at pad 19 (MIDI 55) = pad-6 copy with decay 0.25 / size 0.15 /
  > pan 0.35 (choke group 1 shared with the sus-cymbal set; body sits below the washBlend
  > gate by design — the ~1 s violet noise layer carries the splash sizzle); tubular bell →
  > pad 23 (MIDI 59). **MIDI 39 decision: castanet stand-in** (GS/XG orchestral tradition),
  > Shell/Impulse: material 0.30, size 0.18 (f0 ≈ 1 kHz clack), decay 0.10, b1 0.55/b3 0.20,
  > click 0.75/contact 0.06/brightness 0.85, scatter 0.15, level 0.75, pan 0.45. Castanet
  > values are a first voicing (no archetype doc exists) — audition and refine by ear.
  > `k.crafted` now {0..19, 23} (21 pads).
- **Plan-doc hygiene (05-orchestralKit.md), one pass:** maxPolyphony 20→16; delete/rewrite stale deviation #6 (crash archetype no longer specifies inject 0.25 post-ea89c6c2 — generator's 0 agrees with the CURRENT archetype); remove deviation #7 (timpani inject) and #4 (airLoading 0.80) per §4.1; add NEW deviations: ride decay/wash rebalance (§4.7), gong Plate swap + 0.4 s morph (§4.6), sus-cymbal struck role (decay 0.85 vs archetype closed 0.15); sync pad-1 spec (§4.10), pad-4 b1 line (0.30→0.05); record metallic levels and the GM remap.

## 7. Constraint compliance

| Constraint | Status |
|---|---|
| `maxPolyphony ∈ [4,16]` | Generator ships 16 — **compliant**. OLD plan doc says 20 — **violation, doc-side**; fixed in §6. No proposed change touches it. |
| `modeInject > 0` rings undamped | **Current kit VIOLATES on 7 pads** (0/5/7/9/11/14 @ 0.10–0.12, 13 @ 0.15). This plan zeroes ALL seven (§4.1, §4.6) — after the fixes, no pad in the kit has modeInject > 0. D2 (envelope) + D3 (NoteOn-only test) harden the constraint engine-wide. No other proposed change introduces inject. |
| Friction sustains without note-off | No pad uses Friction (exciters: Mallet/NoiseBurst/Impulse throughout, incl. the new splash-role and stand-in pads). **Compliant.** |
| Kit categories fixed | Kit stays `Acoustic`; filesystem subdir + XML metadata unchanged. **Compliant.** |
| All valueNorm ∈ [0,1] | Every proposed normalized value checked: max is 1.00 (airLoading, noiseLayerDecay), min 0.0 (modeInject, b3). `FilterType::HP = 0.5` ∈ [0,1]. **Exception by design:** `noiseLayerGain 6.2` is a raw F64 blob field (serialization index 57), NOT a valueNorm — legal, bit-identical to the three shipping sibling snares. **Compliant.** |
| chokeGroup/outputBus ints | Unchanged (pad 6/8/10 chokeGroup 1, aux bus 1 assignments carried over on relocated pads). **Compliant.** |

## 8. Implementation order

**(a) Pure generator value fixes** — one edit pass over `tools/membrum_preset_generator.cpp` `orchestralKit()`: §4.1 (modeInject 0, strikePos 0.83, airLoading 1.0, tom noise color/decay), §4.2 (snare gain/wire/strikePos), §4.3 (b1 0.05, strikePos 0.35), §4.4 (pads 6/8 tables), §4.5 (pad 10), §4.6 (gong incl. Plate swap), §4.7 (ride), §4.8 (triangle), §4.9 (crotales), §4.10 (bell tree). Same commit: 05-orchestralKit.md sync + archetype doc corrections (timpani strike-pos table, bell-tree/crotales decaySkew sign wording, ride deviation entry).

**(b) Mapping/naming** — GM remap per §6 (6th timpani at pad 12, triangle→18, tubular bell→23, crotales-lo→16, splash-role at 19, MIDI-39 stand-in decision, `k.crafted` update) as a kit-design update to 05-orchestralKit.md first, then the generator.

**(c) DSP work items** — **D1 (harness fidelity) FIRST — it gates all of (e).** Then D9-comment, D4 (mallet contact hint), D2 (inject envelope), D3 (NoteOn-only test). D5/D6/D7/D9-reconciliation as separate gated specs. D8 only if the post-D1 timpani re-measure still shows the undertone.

**(d) Regenerate + install** — build the preset-generator target, regenerate the kit, install the plugin via the build target (never hand-copy VST3 files), copy regenerated presets to `C:/ProgramData/Krate Audio/Membrum/Kits/Acoustic/`.

**(e) Verification** — §9. Any threshold that fails: fix before commit (no "pre-existing" waivers).

## 9. Verification recipe

Tooling (after D1 lands — until then every noise/click/damping/inject/morph number is untrustworthy):

```
# 1) Re-transcribe the EDITED generator values into per-pad JSON (update the script's
#    pad tables to match the new generator values first):
node C:\Users\scab\AppData\Local\Temp\claude\f--projects-iterum\8cda256f-f17e-4693-aa45-3677a41bead1\scratchpad\gen_orchestral_pads.js

# 2) Render each crafted pad (usage: membrum-fit-gen <input.json> <output.wav> [--sr 44100] [--sec 2.0] [--vel 1.0]):
f:/projects/iterum/build/windows-x64-release/bin/Release/membrum_fit_gen.exe ^
  <scratchpad>\pads\padNN.json <scratchpad>\renders\padNN.wav --sr 48000 --sec 6.0
#   pads 10, 13, 15: use --sec 10.0 (3 s wash / long tails were mis-read in 4 s renders)

# 3) Extract features:
node C:\Users\scab\AppData\Local\Temp\claude\f--projects-iterum\8cda256f-f17e-4693-aa45-3677a41bead1\scratchpad\wav-features.js <scratchpad>\renders\padNN.wav
```

Per-pad post-fix thresholds (keys: `centroidHz`, `flatnessHigh`, `dominantPeaks`, `t60s`, `tailFloorDb`, `attackMs`):

| Pad(s) | Must hold post-fix |
|---|---|
| 0, 5, 7, 9, 11, 14 | `dominantPeaks` (m,1) ratios within ±3% of 1 : 1.50 : 2.00 : 2.44 (airLoading 1.0; was 1.56–1.59/2.22–2.27/2.79–2.88); (1,1) is the strongest post-onset peak (was −6.6 dB under (0,1)); `t60s` 0.8–1.6 s (production b1 path; was 4.4–4.6 s harness artifact); NO flat tail plateau: `tailFloorDb ≤ −60` at 6 s |
| 2 | 2–8 kHz band fraction ≥ 0.25 (was 0.0017) — *in-plugin render as final arbiter* (buzz path); `t60s` ≤ 0.4 s (b1 0.62 → 0.22 s); woodblock peak set 689/539/373/234 Hz no longer dominant |
| 4 | `t60s` 2.2–3.0 s (target ~2.57 s, was 0.46 s in-plugin); post-onset dominant peak settling toward ~40 Hz (ignore full-file smear) |
| 6 | No single `dominantPeaks` line ≥ 12 dB above its neighbors near ~670 Hz (was lone 948 Hz ping); `flatnessHigh ≥ 0.5`; `t60s` 2.0–4.0 s (was 0.3 s in-plugin / 57 ms sizzle) |
| 8 | `t60s` ≤ 0.6 s; noise-layer chick audible (band energy > 0 above 4 kHz) |
| 10 | Still passes: `flatnessHigh ≥ 0.7` (was 0.765); with 10 s render `tailFloorDb ≤ −50` (clears the old elevated-tail artifact); morph swell visible in envelope (peak not at onset) |
| 12 | ≥ 2 kHz fraction dominant once click layer is forwarded; highest shell partial (~6.7 kHz) `t60` ≥ 2 s (was 7 ms); `t60s` 4–7 s |
| 13 | 2–8 kHz energy fraction > 0.05 (was 0.0); lowest partial 90–105 Hz (Plate f0 ≈ 96 Hz); NO flat plateau: strictly monotone tail decay, `tailFloorDb ≤ −60` at 10 s (inject removed) |
| 15 | `t60s` ≤ 4.5 s (was 8–13 s); wash duration ≥ 1.8 s (noiseLayerDecay at the 2000 ms ceiling); `flatnessHigh` up vs baseline render |
| 17, 19 | Dominant peak is NO LONGER 151.7 / 120.5 Hz (hum suppressed ≥ 20 dB below the nominal/octave partials); `t60s` 2.5–4.0 s and ≠ 999 flag |
| 1 | Top `dominantPeaks` no longer the 0.25–0.75× hum set (159.1/319.1/384.6/484.2 Hz); `centroidHz ≥ 2000` (violet sizzle through the 13 kHz LP, was 632 Hz LP); sizzle tail ~1.1 s |

Then the plugin-side gate (per project rules):

```
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target membrum_tests
build/windows-x64-release/bin/Release/membrum_tests.exe 2>&1 | tail -5     # ALL pass, incl.
#   "Per-pad single hit decays to silence on Orchestral" (passes trivially with inject 0)
#   factory round-trip tests, kit-switch infinite-ring tests (+ new D3 NoteOn-only scenario)
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3"
```

Final by-ear A/B in a DAW after copying presets to `C:/ProgramData/Krate Audio/Membrum/Kits/Acoustic/` — the harness excludes the coupling engine, so the gong-coupling watchlist (§6) and snare buzz feel are ear/in-plugin calls.

## 10. Appendix

### Rejected findings (do NOT implement)

| Finding | Why rejected |
|---|---|
| Snare secondary-shell removal (snare F1) | Explicitly documented deliberate deviation (kit doc line 43, divergence list line 90, verification item 3: "audibly tom-free at this coupling/size"). Self-refuting evidence: the shell was NOT in the measured render, yet the woodblock peaks appeared anyway — the shell isn't the cause. Re-open only with post-buzz-fix A/B evidence. |
| Timpani (0,n) amplitude hack keyed on airLoading (timpani F3 stage 1) | airLoading 0.6–0.8 ships on Membrane pads across ALL kits — would cut kick/tom fundamentals kit-wide 36–48% and stack on the strike-position fix. Stage 2 deferred as D8. |
| Gran-cassa airLoading 0.80 → 1.00 (bass F3) | Documented deliberate deviation with recorded rationale, re-chosen post-N-1; pad 4's 0.80 exactly matches its own gran-cassa archetype (line 32). Out of the group's remit; premise ("leftover clipping workaround") contradicted by the plan record. |
| Remove gran-cassa pitch env 110→40 Hz (bass F4) | Generator matches the approved archetype numerically (0.3702/0.1505/0.24/0.15); the boom-glide is the archetype's own documented design. The "no synthetic 808 boom" prohibition is from the TIMPANI archetype. Low-confidence finding conditioned on an STFT test never run. |
| Gran-cassa level 0.88 → 0.85 and noiseLayerColor 0.12 → 0.10 (bass F5, partial) | Level 0.88 is the kit plan's deliberate post-audit headroom restoration; color 0.10 vs 0.12 is a same-Brown-band DSP no-op that would break the generator's house Brown consistency. |
| Snare noiseLayerColor 0.90 → 0.85, material/size retune (snare F3, partial) | Color: provable no-op (≥0.80 all decode Violet). Material 0.60 / size 0.33: approved kit-plan values with no measurement implicating them. |
| washBlend DSP change `(decay-0.35)*2` (sus F4) | No cliff exists (clamp is continuous); the pads-6/8 preset fix escapes the dead zone anyway. Unbounded blast radius: silently retunes every NoiseBody preset with decay ∈ (0.35, 0.925) across ~20 kits, including the just-shipped crash redesign (ea89c6c2) and its goldens. If ever wanted: own spec with dead-zone-only formulation and full factory re-verification. |
| Gong secondary-shell trio (ride-gong F4, partial) | Documented deliberate deviation (kit doc pad-13 note + divergence list + verification item 5) with a stated RMS pile-up rationale the finding never answers. (Its couplingAmount-vs-couplingStrength distinction was correct but only confirms the plan chose OFF knowingly.) |
| Velocity-mapped mallet mass (small-metals F2 as proposed) | `mallet_exciter.h:53-56` records this exact approach tried and reverted (breaks 10 ms centroid window / velocity-ratio tests). Replaced by D4's body-size contact hint. |

### Adjudicated conflict

Kit-level audit recommended KEEPING timpani modeInject 0.10–0.12 (documented, sibling precedent) contingent on new test coverage. The timpani group pipeline CONFIRMED zeroing it, explicitly answering the deviation's rationale: the hard constraint (undamped plateau) supersedes, the archetype mandates 0, the deviation text concedes the synthetic series competes with the Bessel pitch, and the identical regression was already fixed on the crash pad. **Resolution: zero it (§4.1).** D2 (inject envelope) is the path to ever re-enabling it.

### Documented deviations deliberately kept

- Snare tight 14×5 secondary shell (deviation, kit doc line 90).
- Bell tree size 0.10 (deviation #8).
- Gong secondary OFF (deviation + RMS pile-up rationale).
- Gran-cassa airLoading 0.80 and pitch env (archetype-faithful).
- 36-timpani/40-gran-cassa GM swap (deviation #12) and the 42/44/46/49/51 role assignments.
- Kit-level coupling values (all in the acoustic band).

### Deviations REMOVED by this plan (previously documented, now refuted from code)

- #7 timpani modeInject activation (hard constraint + archetype line 50).
- #4 timpani airLoading 0.80 ("over-thinning" has no code mechanism — frequency-only on 4 modes).
- #6 stale crash inject-0.25 citation (describes a pre-redesign archetype; no longer a deviation at all).

### Missing-evidence groups

None — all six per-group pipelines (timpani-row, snare, bass-drum, sus-cymbals, ride-gong, small-metals) and the kit-level/GM audit completed with adversarial verification. No group requires manual audit.