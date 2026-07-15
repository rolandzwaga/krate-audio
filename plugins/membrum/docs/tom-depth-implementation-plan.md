# Implementation Plan — Fix Membrum's Thin, Toy-Like Toms

Target repo: `f:/projects/iterum` · Plugin: `plugins/membrum/` · Branch: work on `feature/membrum-tom-depth`. Build target: `membrum_tests`. Build tool: `"C:/Program Files/CMake/bin/cmake.exe"`, preset `windows-x64-release`.

All line numbers below are approximate anchors — locate by the quoted enclosing function/comment, not the number.

---

## Background (why the toms sound like toys)

Rendered ground truth (built-in default kit, MIDI 41/43/45/47/48/50) shows fundamentals track the membrane law `f0 = 500*0.1^size` correctly (79–200 Hz) and the low toms carry real low weight. The toy character comes from four independently-confirmed defects:

1. **No downward pitch glide (H1).** The Tom template sets `tensionModAmt=1.0` but never sets a ToneShaper pitch envelope (`tsPitchEnvTime` stays 0, so `isPitchEnvActive()` is false). The only pitch motion is the tension energy-follower `1 + amt*energyEnv_`, where `energyEnv_` starts at 0 and *rises* with modal energy → a tiny **upward** drift (+2 Hz over 300 ms). A real tom drops. There is also no percussive onset: amplitude peaks ~20 ms after t=0 (attackRatio 0.36 — a swell, not a strike) and the click is dark/buried (2 k–8 k band ≈ 0.000 across the whole render).
2. **Energy parked in a weakly-damped inharmonic 100–500 Hz mid cluster (H2).** The `f²` damping term (`b3`) is negligible for toms, so the 126–383 Hz membrane modes ring nearly as long as the fundamental. Setting `b3` to norm 0.15 was **rendered** and collapses the 100–500 band from 0.317 → 0.164 and drops the centroid 160 → 129 Hz. Strike-to-center was tested and **refuted** — do not touch `strikePosition`.
3. **Installed factory kits are worse (H3).** `tools/membrum_preset_generator.cpp` writes toms with `bodyDampingB1≈0.30–0.40` + `bodyDampingB3=0.10` (T60 ≈ 0.3 s, ~6× over-damped) and `tomPitchEnd=110..290 Hz` — pulling the floor tom UP a musical fourth from its natural 79 Hz. The supplied renders are the built-in kit; what users load is more severe.
4. **Peak-normalization inverts the size→weight law (H5).** Every pad is scaled so its broadband attack peak lands at −6 dBFS; the floor tom rings up more (higher crest factor) so it gets attenuated the most. Measured: the 16-inch floor tom is the **quietest** body of the row by ~4–5 dB.

Targets (from acoustics literature): (0,1) fundamental dominant by ≥6–12 dB after 50 ms; fundamental T60 1.5–3.0 s for floor toms; overtone T60 0.15–0.5 s (ratio ≥3–5×); a velocity-scaled **downward** glide starting +3…+10 % above f0 settling over 50–300 ms; genuine 60–150 Hz sustain (do not high-pass); bigger drum = louder/weightier.

**Design principle that keeps this safe:** every fix in the executed set is a *preset-data* change (`default_kit.h` + `membrum_preset_generator.cpp`) plus one pure-function classification edit. **No audio-thread DSP is touched, and the shared `MembraneMapper` is deliberately left alone** (see the *Deferred: Fix C* section for why). Because the tom pads use the Mallet exciter, they always take `processBlockFast`; the `processBlockSlow` branch is only for the Feedback exciter and is not exercised by toms (`drum_voice.h`, `const bool useSlowPath = feedbackExciter;`).

---

## Fix A — Give toms a real downward pitch glide + articulate onset (H1)

**Impact: highest.** This is the single most audible change (adds the "tonk").

### A.1 Enable a per-pad downward ToneShaper pitch envelope on the six tom pads

The pitch-env start/end are **absolute** log-Hz norms, so they cannot live in the shared template (all six toms would get one pitch). Set them **per pad** after the template-application loop.

**File:** `plugins/membrum/src/dsp/default_kit.h`, function `inline void apply(std::array<PadConfig, kNumPads>& pads)` (anchor: the `kSpecs[kNumPads]` array and the loop `applyTemplate(pads[i], kSpecs[i].tmpl, kSpecs[i].sizeOverride);`).

The six tom pad indices are **5, 7, 9, 11, 12, 14** with `sizeOverride` **0.8, 0.7, 0.6, 0.5, 0.45, 0.4** (verified in `kSpecs[]`). Insert this block **after** the `applyTemplate` loop, before the end of `apply()`:

```cpp
// --- Tom depth pass (fixes thin/toy toms) ---------------------------------
// Downward "tonk" glide via the ToneShaper pitch envelope. END == the pad's
// natural size-derived f0 (500 * 0.1^size) so the SUSTAINED pitch equals the
// membrane's natural fundamental (no up-tuning); START sits a few % above so
// the whole modal bank glides DOWN onto f0. Encoding matches the Kick:
//   norm = log(Hz / 20) / log(100)   (20 Hz -> 0, 2000 Hz -> 1)
{
    // Pad indices 5,7,9,11,12,14 = MIDI 41,43,45,47,48,50 (sizes 0.8..0.4).
    constexpr int   tomPad[6]   = {5, 7, 9, 11, 12, 14};
    // Natural f0 = 500 * 0.1^size for size {0.8,0.7,0.6,0.5,0.45,0.4}:
    constexpr float tomEndHz[6] = {79.24f, 99.76f, 125.59f, 158.11f, 177.42f, 199.05f};
    // Start = f0 * (1 + glide%), graded +10% (floor) down to +4% (high):
    constexpr float tomStartHz[6] = {87.16f, 108.74f, 135.64f, 167.60f, 186.29f, 207.01f};
    // Glide time normalized (ms / 500): 250,220,190,160,140,120 ms.
    constexpr float tomTimeN[6] = {0.50f, 0.44f, 0.38f, 0.32f, 0.28f, 0.24f};
    // Fix D: bigger tom = louder/weightier (counteracts the peak-normalizer
    // inverting the size->weight law). Floor tom loudest, high tom quietest.
    constexpr float tomLevel[6] = {0.95f, 0.88f, 0.82f, 0.76f, 0.71f, 0.67f};
    auto toLogNorm = [](float hz) {
        return static_cast<float>(std::log(hz / 20.0f) / std::log(100.0f));
    };
    for (int i = 0; i < 6; ++i) {
        PadConfig& c = pads[static_cast<std::size_t>(tomPad[i])];
        c.tsPitchEnvStart = toLogNorm(tomStartHz[i]);
        c.tsPitchEnvEnd   = toLogNorm(tomEndHz[i]);
        c.tsPitchEnvTime  = tomTimeN[i];   // enables isPitchEnvActive()
        c.tsPitchEnvCurve = 0.0f;          // 0 = exponential settle
        c.tensionModAmt   = 0.25f;         // Fix A.2 (see below)
        c.level           = tomLevel[i];   // Fix D (see below)
    }
}
```

Ensure `<cmath>` is included at the top of `default_kit.h` (it is used for `std::log`/`std::pow` in this subtree; add the include if the build reports it missing).

**Why END = natural f0 and not higher:** `ToneShaper::processPitchEnvelope()` returns `endHz` once the envelope has fully elapsed, and `retuneFundamental()` scales the whole bank by `pitchHz / naturalFundamentalHz_`. With END = f0 the sustained ratio is 1.0 → the bank sits at its natural pitch; START above f0 glides every mode down proportionally (satisfies "applied proportionally to every mode").

### A.2 Lower `tensionModAmt` in the Tom template

The loop above already sets `c.tensionModAmt = 0.25f` per tom pad. Also change the template default so the value is consistent for any code path that reads it before the loop.

**File:** `plugins/membrum/src/dsp/default_kit.h`, `case DrumTemplate::Tom:` (anchor: comment `// Phase 8E: toms are the canonical "kerthump" case`).

- BEFORE: `cfg.tensionModAmt = 1.0f;`
- AFTER: `cfg.tensionModAmt = 0.25f;`

Rationale: with the pitch env active, `processBlockFast` multiplies the downward env by `tensionPitchMod = 1 + tensionAmtEffective_*energyEnv_`. Keeping tension at 1.0 re-introduces a small upward bump. 0.25 leaves a subtle velocity-dependent "give" without reversing the glide.

### A.3 Brighten the mallet click so a transient lands at t=0

**File:** `plugins/membrum/src/dsp/default_kit.h`, `case DrumTemplate::Tom:` (anchor: `// Phase 7.1: head resonance + felt-mallet click.`).

- `cfg.clickLayerMix`: `0.7f` → `0.85f`
- `cfg.clickLayerContactMs`: `0.3f` → `0.12f`  (shorter contact → more HF)
- `cfg.clickLayerBrightness`: `0.45f` → `0.80f`

**Routing note — the filter is NOT the problem.** The Tom template never sets `tsFilterType`/`tsFilterCutoff`, so they stay at the PadConfig defaults `tsFilterType = 0.0` (low-pass) and `tsFilterCutoff = 1.0` (fully open — verified in `pad_config.h`). The click's highs already pass unfiltered. **Do NOT touch the tom filter cutoff** (raising or lowering it is unnecessary and off-spec — do not high-pass or scoop the lows). After building, render MIDI 41 (recipe in Verification) and confirm the **2 k–8 k band > 0** in the first 20 ms. If it is still ~0.000, the fix is in the click-layer parameters above (raise `clickLayerBrightness` toward 1.0 / shorten `clickLayerContactMs`), not in the ToneShaper.

### A.4 Keep toms classified as Tom (not Kick) after enabling pitch env

`classifyPad()` currently returns `PadCategory::Kick` for **any** Membrane pad with `tsPitchEnvTime > 0` (verified: `pad_category.h` Rule 1). So A.1 would silently reclassify all six toms as Kick, changing the coupling matrix. (Note: the *installed* generator toms already set a pitch env + Mallet, so they are **currently** mis-classified as Kick; this fix corrects them to Tom too — an intended behavior change.)

**File:** `plugins/membrum/src/dsp/pad_category.h`, function `classifyPad`.

Add a discriminator **before** the existing Kick rule (kicks use `ExciterType::Impulse`, toms use `ExciterType::Mallet`):

```cpp
if (cfg.bodyModel == BodyModelType::Membrane) {
    // Rule 0: Membrane + Mallet exciter = a Tom, even with a pitch env
    // (the tom "tonk" glide). Distinguishes it from the Kick, which uses
    // the Impulse exciter with a pitch env.
    if (cfg.exciterType == ExciterType::Mallet)
        return PadCategory::Tom;
    // Rule 1: Membrane + pitch envelope active -> Kick
    if (cfg.tsPitchEnvTime > 0.0f)
        return PadCategory::Kick;
    // Rule 2: Membrane + NoiseBurst exciter -> Snare
    if (cfg.exciterType == ExciterType::NoiseBurst)
        return PadCategory::Snare;
    // Rule 3: Membrane only -> Tom
    return PadCategory::Tom;
}
```

The existing `classifyPad` Kick tests set `exciterType = ExciterType::Impulse` (Rule-1 case) and `ExciterType::NoiseBurst` (priority case) — verified — so neither is Mallet and both still return Kick. No existing category assertion sets Mallet+pitchEnv expecting Kick, so Rule 0 does not break them. Add a new positive case (see Tests §).

### Expected effect of Fix A
- Pitch glide flips to **downward**, becomes audible (≥ 1 semitone on the floor tom): MIDI 41 ~87 Hz start settling to ~79 Hz (currently +2 Hz upward).
- Onset: first-5 ms / overall peak ratio rises from 0.36 toward ≥ 0.7; measurable 2 k–8 k energy appears in the first ~20 ms.
- Sustained fundamental stays at the natural ~79 Hz (not up-tuned).

### Tests affected by Fix A (verify each honestly)
**Will break — update expectations:**
- `plugins/membrum/tests/unit/processor/test_default_kit.cpp` — any assertion that toms have no pitch env, that `tensionModAmt==1.0`, or `level==0.8`. Update to `tsPitchEnvTime>0`, `tsPitchEnvEnd` decodes to natural f0, `tensionModAmt==0.25`, and the per-pad `tomLevel[]`.
- `plugins/membrum/tests/unit/dsp/test_pad_category.cpp` — add the new Mallet→Tom positive case; the existing Kick/Snare cases already set non-Mallet exciters and stay green.

**Must verify (may or may not break depending on what they assert):**
- `plugins/membrum/tests/unit/voice_pool/test_per_pad_pitch_env.cpp` — configures pad 5 with its **own** pitch env (does not read the default-kit tom values), so it likely stays green; confirm it does not additionally assert tom **category** == Tom via the coupling path. If it asserts pad 5 classifies as anything, update to Tom.
- `plugins/membrum/tests/unit/controller/test_pitch_env_body_gate.cpp` — controller-side pitch-env gate. Confirm it does not assert the tom pads are pitch-env-**inactive**; if it does, update.
- `plugins/membrum/tests/unit/dsp/test_pitch_env_all_bodies.cpp` — exercises DrumVoice pitch env directly (no tom/category references found); expected unaffected — confirm it still passes.
- `plugins/membrum/tests/unit/voice_pool/test_per_pad_dispatch.cpp` and any factory-load / coupling-matrix test that asserts a tom pad's `PadCategory` — these now see Tom (was Kick for installed-style configs). Update expected category to `Tom`.
- `plugins/membrum/tests/unit/tone_shaper/test_pitch_envelope_808.cpp`, `plugins/membrum/tests/unit/ui/test_pitch_envelope_display.cpp` — unit/UI tests not tied to tom pads; expected unaffected — confirm.

Run the full `membrum_tests` suite after Fix A and reconcile **every** red case against this list; do not dismiss any as pre-existing.

---

## Fix B — Collapse the inharmonic mid cluster with frequency-dependent damping (H2)

**Impact: high (rendered).** This is the primary "body-blooms-to-low-tail" lever, and it is the exact configuration that was rendered in verification (b3 norm 0.15 on top of the built-in legacy `b1`).

**File:** `plugins/membrum/src/dsp/default_kit.h`, `case DrumTemplate::Tom:` (anchor: `// Phase 8C: tom-leaning air-loading + light scatter.`).

Add a non-sentinel `bodyDampingB3`, leaving `bodyDampingB1` at its sentinel `-1.0f` so the fundamental stays on the legacy long-decay path:

```cpp
// Frequency-dependent damping: the f^2 term dissolves the 100-500 Hz
// inharmonic mid modes while the low fundamental (weak f^2 term) keeps its
// long ring. Stored as the NORMALIZED value; the mapper denormalizes it to
// b3 = norm * 1.0e-3 (verified: dampingLawFromParams in membrane_mapper.h).
// bodyDampingB1 left sentinel (-1) so b1 stays on the legacy long-decay path.
cfg.bodyDampingB3 = 0.15f;
```

**Numeric reasoning (anchored on the measured baseline, not a derived b1 guess).**
`dampingLawFromParams` (verified) sets `b3 = norm * 1.0e-3` → `0.15 * 1e-3 = 1.5e-4`. The bank uses `decayRate_k = b1 + b3*f_k²` and `T60 = ln(1000)/decayRate = 6.908/decayRate`.

- **Fundamental (f0 = 79 Hz), built-in floor tom.** The *measured* baseline fundamental T60 is **1.92 s** (from the ground-truth renders), i.e. the legacy path's effective `decayRate ≈ 6.908/1.92 ≈ 3.60 s⁻¹` (this is `b1`, since baseline `b3≈0`). Adding `b3*f0² = 1.5e-4 * 79² = 0.94 s⁻¹` gives `decayRate ≈ 4.54` → **T60 ≈ 6.908/4.54 ≈ 1.52 s** — still above the 1.5 s floor, but only just. Do not raise `b3` past 0.15 on the built-in tom or the fundamental drops below target. (Treat 1.52 s as a computed estimate; the render in Verification is the ground truth. The generator toms in Fix E get a lower `b1` override for more headroom.)
- **Mid mode at 231 Hz.** `b3*f² = 1.5e-4 * 231² = 8.0 s⁻¹` → `decayRate ≈ 3.6 + 8.0 = 11.6` → **T60 ≈ 0.60 s** (was ~1.9 s). Overtone/fundamental T60 ratio ≈ 2.5× — an audible bloom toward a pure low tail, though short of the ≥3× literature ideal (the built-in legacy `b1` limits headroom; the generator kit in Fix E does better).

Do **not** set `bodyDampingB1` here (leaving it sentinel is load-bearing — an override would clobber the long fundamental).

### Expected effect of Fix B (rendered targets)
MIDI 41: 100–500 Hz band **0.317 → ~0.16**, 20–100 Hz band **0.683 → ~0.84**, spectral centroid **160 → ~129 Hz**, fundamental T60 **1.92 → ~1.5 s** (stays ≥ 1.4 s). Audible "bloom" from a mid ring into a pure low tail.

### Tests affected by Fix B
- `plugins/membrum/tests/unit/dsp/test_per_mode_damping.cpp` — if any case pins a tom-configured bank's `b3` or asserts the sentinel. Update the expected **normalized** field to `0.15f` where the tom config is built, and the **denormalized** bank `b3` to `1.5e-4` where the mapper output is asserted.
- `test_default_kit.cpp` — add/adjust the tom `bodyDampingB3 == 0.15f` assertion (see Tests §; the field stores the *norm*).

---

## Deferred: Fix C — Shared-mapper input roll-off (DO NOT IMPLEMENT this pass)

The original plan proposed rolling off the input mode gains inside `MembraneMapper::map()` so the (0,1) fundamental dominates. **This is deliberately excluded from the executed set.** Reasons the implementer must understand (do not "add it anyway"):

- `MembraneMapper::map()` is shared by **every Membrane pad in every kit** — all kicks, all snares, all toms across Acoustic/Electronic/Percussive/Unnatural. An unconditional roll-off in `map()` changes the input spectrum of all of them, not just factory toms. Its regression surface is unbounded from the plan.
- It breaks the committed approval golden. `plugins/membrum/tests/approval/test_phase1_regression.cpp` renders the default patch (Impulse + Membrane, size 0.5, strikePos 0.3) through `map()` and compares against `plugins/membrum/tests/golden/phase1_default.bin` within −90 dBFS RMS. Any `map()` amplitude change mismatches that golden and requires re-baselining via the hidden case `membrum_tests.exe "[.generate_golden]"` — plus re-baselining every kick/snare/body-amplitude golden (`test_membrane_body.cpp`, etc.).
- Fix B **already meets** the measurable 100–500 Hz band target (0.317 → 0.164, rendered). The marginal extra "(0,1) dominance" from a mapper roll-off does not justify a global, golden-shifting audio-thread edit.

If a future pass genuinely needs it, the **only** acceptable form is a per-pad, default-OFF gate: add a normalized field (e.g. `driveHFRolloff = 0.0f`, default 0 = bit-identical) to `PadConfig` **and** `VoiceCommonParams` (populated in `DrumVoice::makeBodyParams`, `drum_voice.h` ~L1509), applied in `map()` only when `> 0`, and set `> 0` on the six tom pads only. With the default patch at 0, `phase1_default.bin` stays unchanged and the blast radius is bounded to the opted-in toms. That is out of scope here.

**Consequence for this pass:** `test_phase1_regression.cpp` must remain **green and unchanged** — you are not touching `map()`. If it goes red, you have accidentally modified the mapper; revert that change.

---

## Fix D — Restore the size→weight law (H5)

**Impact: medium. Zero code risk (preset-only).** The peak-normalizer makes the floor tom the quietest body by ~4–5 dB. Rather than rewriting the normalizer (which would re-level every pad in the plugin), grade the per-pad `level` so bigger toms are louder.

**Already applied** by the `tomLevel[6]` table in the Fix A.1 loop (`c.level = tomLevel[i]`). Values: `{0.95, 0.88, 0.82, 0.76, 0.71, 0.67}` — a `0.95/0.67 = 1.42 ≈ +3 dB` spread that partially offsets the measured ~4–5 dB inversion. Keep all values ≤ 0.95 (the voice ends in a `softClip`; > 1 risks clipping the transient).

### Expected effect
Floor-tom sustained RMS rises above the high tom's; the row reads as graded weight rather than uniform level.

### Tests affected by Fix D
- `test_default_kit.cpp` "Tom template base parameters" — asserts `level == 0.8` for every tom. Update to the per-pad `tomLevel[]` expectations.

### Optional advanced alternative (do NOT do unless Fix D proves insufficient after A/B)
Switching the primary-body normalizer from instantaneous peak to a sustained-fundamental / longer-window RMS target in `drum_voice.h` (`probeStrikePeakExcited()` / `measuredStrikeOutputGain()`) affects **every pad** and every kit's level balance — out of scope for this pass; note as a follow-up only.

---

## Fix E — Regenerate the installed factory kits (H3)

**Impact: high for real users** (installed kits are what loads, and they are more over-damped/up-tuned than the built-in kit).

**File:** `tools/membrum_preset_generator.cpp`. There are multiple per-kit tom loops. Apply the transform below to **every Acoustic-category tom loop that is over-damped/up-tuned** (identify by `tomB1[]` values ≥ 0.30 and/or `tomPitchEnd[]` above the natural-f0 row). The **Electronic TR-808 kit** tom loop is already correct — **use it as the reference and leave it unchanged.**

**Worked example — Acoustic Studio Kit loop** (anchor: `// ---- Toms (Membrane/Mallet) -- true per-pad graded gradient ----`; verified arrays: `tomSizes = {0.80,0.70,0.60,0.55,0.45,0.40}`, `tomPitchStart`, `tomPitchEnd`, `tomPitchTime`, `tomB1`, and `pads[p].bodyDampingB3 = 0.10;`). Note `tomSizes[3] = 0.55`, whose natural f0 is **141 Hz** (not 150).

1. **Pitch END → natural f0** (`round(500*0.1^size)` for this loop's `tomSizes`):
   - BEFORE `tomPitchEnd = {110, 130, 150, 180, 230, 290}`
   - AFTER  `tomPitchEnd = {79, 100, 126, 141, 177, 199}`   ← 4th entry is **141** (size 0.55), recompute per the loop's own `tomSizes`; never copy 150.
2. **Pitch START → a few % above the new END** (glide down):
   - AFTER  `tomPitchStart = {84, 106, 134, 149, 188, 211}`  (≈ END × 1.06; 4th = 141×1.06 = 149).
3. **`tomB1` → low, constant override so the fundamental is long; the b3 f² term supplies the T60 gradient.**
   - BEFORE `tomB1 = {0.30, 0.32, 0.34, 0.34, 0.37, 0.40}`
   - AFTER  `tomB1 = {0.055, 0.055, 0.055, 0.055, 0.055, 0.055}`
   Reasoning (override path `b1 = 0.2 + norm*49.8`): norm 0.055 → `b1 = 2.94 s⁻¹`. Combined with `b3 = 1.5e-4` (`decayRate = b1 + b3*f0²`, `T60 = 6.908/decayRate`) the fundamentals land, top→bottom of the size row:
     - 79 Hz → decayRate 3.88 → **T60 1.78 s**
     - 100 Hz → 4.44 → **1.56 s**
     - 126 Hz → 5.32 → **1.30 s**
     - 141 Hz → 5.92 → **1.17 s**
     - 177 Hz → 7.64 → **0.90 s**
     - 199 Hz → 8.88 → **0.78 s**
   A naturally graded, monotonically-decreasing fundamental T60 (floor 1.78 s in the 1.5–3.0 s floor-tom target; smaller toms in the 0.5–1.5 s rack range). A constant `b1` is intentional — the `b3 f²` term produces the gradient.
4. **`bodyDampingB3` → keep frequency-dependent, match Fix B:** set `pads[p].bodyDampingB3 = 0.15;` (was `0.10`).
5. **Pitch TIME → longer glide** (`ms/500`, 250 ms floor → 120 ms high):
   - BEFORE `tomPitchTime = {0.18, 0.16, 0.14, 0.13, 0.10, 0.08}` (90–40 ms)
   - AFTER  `tomPitchTime = {0.50, 0.44, 0.38, 0.32, 0.28, 0.24}` (250–120 ms)
6. Leave `tensionModAmt` (already 0.22 — good), `strikePosition`, `airLoading`, `modeScatter`, `decaySkew`, `secondary*`, `pan` as-is (H2 refuted strike/air changes; scatter/skew/pan are cosmetic gradients).

Repeat steps 1–5 for the other **Acoustic-category** tom loops (Jazz Brushes, Rock Big Room, Vintage Wood, Orchestral), computing each loop's `tomPitchEnd` from **its own** `tomSizes[]` (always `round(500*0.1^size)` — never copy another loop's numbers). Percussive/Unnatural "plate tom" loops that intentionally use inharmonic Plate bodies with a designed pitch drop keep their character — apply only steps 3–5 (de-over-damp + longer glide) *and only if* their T60 is < 1 s or `pitchEnd` is above natural f0; otherwise leave them untouched.

**After editing the generator you MUST regenerate and reinstall** — the installed `.vstpreset` kits in `C:\ProgramData\Krate Audio\Membrum\Kits\{Acoustic,Electronic,Percussive,Unnatural}\` are NOT rebuilt automatically. Build the preset-generator target and run it, or use the plugin's install/preset build target. **Never hand-copy a single VST3 file** (leaves `Resources/editor.uidesc` missing → blank UI + crash on unload). Kit categories are FIXED (`Acoustic`/`Electronic`/`Percussive`/`Unnatural`) — do not add or rename any.

### Tests affected by Fix E
- Generator round-trip / factory-load tests asserting specific tom pitch/damping values — update expected values to the new arrays.

---

## New tests (target: `membrum_tests`)

Create `plugins/membrum/tests/unit/processor/test_tom_body_depth.cpp` and register it in `plugins/membrum/tests/CMakeLists.txt` (add the `.cpp` to the `membrum_tests` source list following existing entries). Model config asserts on `test_default_kit.cpp` and the render harness on `plugins/membrum/tests/approval/test_phase1_regression.cpp` (which drives `DrumVoice` directly: `prepare()`, setters, `noteOn()`, then a `process()` / `processBlock()` loop).

### Config-level asserts (fast, no render)

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "dsp/default_kit.h"
#include "dsp/pad_config.h"
#include <array>
#include <cmath>
using Catch::Approx;

TEST_CASE("Tom pitch env glides DOWN onto natural f0", "[tom_depth]") {
    std::array<Membrum::PadConfig, Membrum::kNumPads> pads{};
    Membrum::DefaultKit::apply(pads);                 // real entry point
    const int   pad[6]  = {5,7,9,11,12,14};
    const float size[6] = {0.8f,0.7f,0.6f,0.5f,0.45f,0.4f};
    auto fromNorm = [](float n){ return 20.0f * std::pow(100.0f, n); };
    for (int i = 0; i < 6; ++i) {
        const Membrum::PadConfig& c = pads[static_cast<std::size_t>(pad[i])];
        REQUIRE(c.tsPitchEnvTime > 0.0f);                       // env enabled
        const float endHz   = fromNorm(c.tsPitchEnvEnd);
        const float startHz = fromNorm(c.tsPitchEnvStart);
        const float f0 = 500.0f * std::pow(0.1f, size[i]);      // natural
        REQUIRE(endHz   == Approx(f0).epsilon(0.05));           // END == natural f0
        REQUIRE(startHz > endHz);                               // DOWNWARD glide
        REQUIRE(startHz < f0 * 1.15f);                          // subtle, not a boing
        // PadConfig stores the NORMALIZED b3 (denorm b3 = norm*1e-3 in mapper).
        REQUIRE(c.bodyDampingB3 == Approx(0.15f));              // Fix B (norm)
        REQUIRE(c.tensionModAmt == Approx(0.25f));              // Fix A.2
    }
}

TEST_CASE("Tom level grades down with size (size->weight)", "[tom_depth]") {
    std::array<Membrum::PadConfig, Membrum::kNumPads> pads{};
    Membrum::DefaultKit::apply(pads);
    REQUIRE(pads[5].level  > pads[14].level);   // floor louder than high
    REQUIRE(pads[5].level  <= 0.95f);           // softClip headroom
    REQUIRE(pads[7].level  > pads[12].level);   // monotonic across the row
}

TEST_CASE("Tom pads classify as Tom, not Kick", "[tom_depth]") {
    // Rule 0: Membrane + Mallet stays a Tom even with an active pitch env.
    Membrum::PadConfig c{};
    c.bodyModel     = Membrum::BodyModelType::Membrane;
    c.exciterType   = Membrum::ExciterType::Mallet;
    c.tsPitchEnvTime = 0.3f;                     // pitch env active
    REQUIRE(Membrum::classifyPad(c) == Membrum::PadCategory::Tom);
}
```

### Render-based spectral asserts (MIDI 41, ≥ 1.5 s tail, 48 kHz)

Render the floor tom by configuring a `DrumVoice` to the pad-5 config (or drive the processor fixture) and compute bands + T60 + glide with a small in-test Goertzel/FFT + RMS-envelope helper (see `testing-dsp-analysis` skill). **Render at a single fixed block size (e.g. 64 samples) for determinism** — do not mix block sizes inside one render. Assert:
- sub-200 Hz energy ratio ≥ 0.85
- 100–500 Hz band ratio < 0.20 (Fix B target; pre-fix ≈ 0.317)
- fundamental T60 ≥ **1.4 s** (Fix B lands the built-in floor tom at ≈ 1.5 s; the 1.4 s floor allows measurement slack — see Fix B numeric reasoning)
- dominant frequency in the first 60 ms **>** dominant frequency at 250 ms (downward glide direction)
- first-20 ms energy in the 2 k–8 k band > 0 (onset articulation, Fix A.3)

### Path / block-size tests (replaces the invalid "Fast vs Slow" equivalence)

**Do NOT write a "one large 2048-sample block == many 1-sample blocks" sample-exact test for toms — it will fail by design.** Verified in `drum_voice.h`: path selection is `const bool useSlowPath = feedbackExciter;` (Feedback exciter only), so a tom (Mallet exciter) **always** takes `processBlockFast`; the slow path is never exercised. Moreover the fast path refreshes the pitch envelope at **block rate** — it reads the pitch from the first sample of each ≤ `kMaxBlockSize` (2048) chunk, calls `retuneFundamental()` once, then consumes the remaining envelope samples. So a single 2048-sample block freezes the glide at `startHz` while 2048 one-sample blocks glide every sample; the two legitimately diverge far beyond any tight margin. Instead assert the properties the design actually guarantees:

```cpp
TEST_CASE("Tom takes the fast path (slow path is Feedback-only)", "[tom_depth]") {
    // Configure a DrumVoice to the pad-5 tom config, then confirm the exciter
    // is Mallet -> useSlowPath == false. (Assert on the config/exciter type;
    // there is no Feedback exciter on any tom, so processBlockSlow is unused.)
    std::array<Membrum::PadConfig, Membrum::kNumPads> pads{};
    Membrum::DefaultKit::apply(pads);
    REQUIRE(pads[5].exciterType == Membrum::ExciterType::Mallet);
}

TEST_CASE("Tom render is deterministic at a fixed block size", "[tom_depth]") {
    // Same config, same block size, two renders -> bit-identical.
    // Build two identical DrumVoice instances, noteOn(velocity), render N
    // samples in 64-sample blocks each, compare sample-by-sample.
    for (std::size_t n = 0; n < N; ++n)
        REQUIRE(bufA[n] == bufB[n]);   // exact: same code path, same block size
}

TEST_CASE("Tom render is block-size robust (envelope level)", "[tom_depth]") {
    // Render the SAME tom config at block size 64 and block size 512. Because
    // the fast path refreshes pitch at BLOCK rate, the two are NOT sample-exact
    // during the glide -- compare at the ENVELOPE/spectral level instead:
    //   - per-frame RMS (e.g. 10 ms frames) within 0.5 dB, and
    //   - dominant frequency of the sustained tail within ~2 Hz.
    // This catches gross path breakage without demanding bit-exactness the
    // block-rate design does not provide.
    for (std::size_t f = 0; f < numFrames; ++f)
        REQUIRE(rmsDb64[f] == Approx(rmsDb512[f]).margin(0.5));
    REQUIRE(tailHz64 == Approx(tailHz512).margin(2.0));
}
```

Use `Approx().margin()` where floating-point results diverge (MSVC/Clang differ at the 7th–8th decimal). Filter tom tests with the positional tag arg `"[tom_depth]"`.

---

## Verification phase

Run from the repo root. **Capture slow output to a log file on the first run; inspect the log — do not re-run to re-grep.**

1. **Build** (build errors/warnings block everything):
   ```
   "C:/Program Files/CMake/bin/cmake.exe" --preset windows-x64-release
   "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target membrum_tests 2>&1 | tee build-membrum.log
   ```
   Fix **all** warnings (C4244 → `f` suffix, C4267 → explicit cast, C4100 → `[[maybe_unused]]`). Zero warnings required.

2. **Unit tests**:
   ```
   build/windows-x64-release/bin/Release/membrum_tests.exe 2>&1 | tail -5
   build/windows-x64-release/bin/Release/membrum_tests.exe "[tom_depth]" --success 2>&1 | tail -20
   ```
   Last line must read `All tests passed`. **Confirm the approval regression stays green** (we did not touch the mapper):
   ```
   build/windows-x64-release/bin/Release/membrum_tests.exe "[phase1]" 2>&1 | tail -5
   ```
   If `test_phase1_regression` goes red, you accidentally changed the shared signal path — revert until it is green (do **not** re-baseline the golden this pass).

3. **Rebuild the plugin + pluginval** (plugin source changed):
   ```
   "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Membrum 2>&1 | tee build-plugin.log
   tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3" 2>&1 | tee pluginval-membrum.log
   ```
   (A post-build copy permission error to `C:/Program Files/Common Files/VST3/` is benign — the bundle in `build/.../VST3/Release/` is valid.) pluginval must report ALL PASSED.

4. **clang-tidy** (from a VS Developer PowerShell, after `cmake --preset windows-ninja`):
   ```
   ./tools/run-clang-tidy.ps1 -Target membrum -BuildDir build/windows-ninja 2>&1 | tee tidy-membrum.log
   ```
   Fix all warnings it reports (including in the touched shared file `pad_category.h`).

5. **Offline render A/B** (ground truth). Use `build/windows-x64-release/bin/Release/krate-render.exe` (see `tools/membrum-fit/AGENT_GUIDE.md` for the invocation). Render MIDI 41/45/50 at velocity 1.0, ≥ 1.5 s, 48 kHz, **before** (git stash / prior commit) and **after**, into the scratchpad. Analyze with a small Node script (bands + T60 + pitch track). Expected after-vs-before deltas for **MIDI 41** (built-in kit):

   | Metric | Before | After (target) |
   |---|---|---|
   | 100–500 Hz band ratio | 0.317 | < 0.20 |
   | 20–100 Hz band ratio | 0.683 | > 0.80 |
   | Spectral centroid | 160 Hz | ~125–135 Hz |
   | Pitch glide direction | +2 Hz up | downward, ≥ −1 semitone |
   | Sustained fundamental | ~80 Hz | ~79 Hz (unchanged; NOT up-tuned) |
   | Fundamental T60 | 1.92 s | ≥ 1.4 s (≈ 1.5 s) |
   | First-20 ms 2 k–8 k energy | 0.000 | > 0 |
   | Floor-vs-high sustained RMS | floor quieter | floor ≥ high |

   After Fix E regeneration + reinstall, also confirm an **installed** Acoustic floor tom (MIDI 41) rose from T60 ≈ 0.3 s toward ≈ 1.8 s and its sustained pitch dropped from ~110 Hz toward the natural ~79 Hz. (krate-render drives the built-in kit; validating the installed `.vstpreset` requires loading it in a host or through whatever preset-load harness `tools/membrum-fit` exposes.)

6. **Commit** per phase (Fix A+A-tests, Fix B, Fix D, Fix E+regen). New commits only — **never `git commit --amend`**. **Do not push** unless the user explicitly asks.

---

## Constraints checklist (must not violate)

- [ ] **No allocations, locks, exceptions, or I/O on the audio thread.** No audio-thread DSP is modified in this pass; all fixes are preset-data plus one pure-function classifier edit.
- [ ] **Shared `MembraneMapper::map()` is NOT modified** — the approval golden `phase1_default.bin` and all kick/snare goldens stay untouched. `test_phase1_regression` must remain green.
- [ ] **Fast/Slow path:** toms use the Mallet exciter → always `processBlockFast`; do not add any per-block-size or per-path branching. The block-size-robustness test tolerates the by-design block-rate pitch refresh (no sample-exact large-vs-1-sample claim).
- [ ] **`maxPolyphony` stays in [4,16]**; **`modeInject` stays 0** on toms (a non-zero inject rings undamped → flat plateau). Do not enable inject.
- [ ] **Kit categories fixed:** `Acoustic`, `Electronic`, `Percussive`, `Unnatural` — filesystem subdir AND XML metadata must match. Never invent categories.
- [ ] **VST params stay normalized [0,1]** at the boundary; you are not adding any new VST parameter. `bodyDampingB3` is stored as its normalized value (0.15), denormalized in the mapper.
- [ ] **`bodyDampingB1` left sentinel (−1) on the built-in Tom template** (Fix B) — an override there would kill the long fundamental. (The *generator* toms in Fix E legitimately override b1 to a **low** value 0.055 to lengthen, not shorten, the fundamental.)
- [ ] **`strikePosition` unchanged** (0.3 built-in / 0.35 generator) — moving the strike to center was rendered and REFUTED (raises the mid band).
- [ ] **Do not high-pass or scoop the lows** in the ToneShaper; do not touch the tom filter cutoff (it is already fully open by default).
- [ ] **Zero compiler warnings; zero clang-tidy warnings** (including in `pad_category.h`).
- [ ] **Installed presets regenerated + reinstalled via the build target**, never hand-copied.
- [ ] Work on the existing feature branch, not `main`. New commits only, no `--amend`, no push without explicit permission.

---

## Rollout order + risk notes

Apply in this order; each stage is independently buildable/testable:

1. **Fix B** (bodyDampingB3 = 0.15) — lowest risk, highest rendered spectral win, one line. Land + render first to confirm the mid-band collapse baseline (0.317 → ~0.16).
2. **Fix A + Fix D** (pitch glide + click + level grading + classifyPad) — one `default_kit.h` tom pass plus the `pad_category.h` Rule 0. A.4 (classifyPad Rule 0) is a hard dependency of A.1 — land them together, or built-in toms silently reclassify as Kick. Adds the audible "tonk" and restores the weight gradient. Independent of B.
3. **Fix E** (generator + reinstall) — independent of the code fixes but reuses the same natural-f0 / `b3 = 0.15` numbers so built-in and installed kits agree. Land after A/B/D are green so the reference values are settled. Regenerate + reinstall via the build target.

**Not in this pass:** Fix C (shared-mapper roll-off) — deferred; see the *Deferred: Fix C* section. Fix B alone meets the 100–500 Hz band target, so nothing downstream depends on Fix C.

**Independence / coupling summary:**
- **Independent:** B ↔ A, B ↔ D, A ↔ D-values (D rides in A's loop but is conceptually separate), code-fixes ↔ E.
- **Coupled:** A.1 ↔ A.4 (classification must land together); B ↔ E (share the `b3 = 0.15` value); the built-in floor-tom fundamental T60 sits at the low edge (~1.5 s) after B — if a future change shortens it further, lower `b3` slightly rather than adding a b1 override.
- **Highest-uncertainty step:** Fix E's per-loop natural-f0 recomputation — always compute `round(500*0.1^size)` from each loop's own `tomSizes[]`; never copy another loop's array (that is exactly the 141-vs-150 trap for size 0.55).