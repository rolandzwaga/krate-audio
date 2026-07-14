# PLAN: Wire Coupling — modal-energy-coupled snare wire buzz

**Date:** 2026-07-13
**Follows:** INVESTIGATION-snare-body-2026-07-01.md / commit `a1480c6b` (#255)
**Feature:** New per-pad parameter `wireCoupling` [0,1] that modulates the parallel
noise-layer ("wire buzz") amplitude by the body's modal energy. At 0 (default) behavior
is **bit-exact** to today. At 1 the buzz amplitude fully tracks the membrane's
vibration envelope — buzz dies with the head, chokes on note-off, re-excites on flams.

**Physics rationale (from literature review):** real snare wires are driven by head
motion (Bilbao 2012 JASA snare FDTD; Torin PhD). Every practical synth (808/909, Nord
Modular, SOS "Practical Snare Drum Synthesis") uses an *independent* parallel noise
layer like ours; envelope-follower coupling of the noise to the body is the documented
step-up. We already have a gain-invariant modal-energy follower (used by tension mod) —
this feature reuses it.

---

## Design (DSP core)

All changes in `plugins/membrum/src/dsp/drum_voice.h` unless stated.

### New members (add near `noiseLayerGain_`, drum_voice.h:1591)

```cpp
// Wire coupling: modulate the parallel noise layer ("wire buzz") by the
// body's modal energy so the buzz tracks the head's vibration envelope
// (Bilbao 2012: snare wires are driven by head motion). 0 = fixed ADSR
// buzz (bit-exact legacy), 1 = full tracking. Block-rate, RT-safe.
float wireCoupling_   = 0.0f;  // per-pad depth (norm)
float wireGainMod_    = 1.0f;  // block-rate gain modulation, applied to noise layer
float wireEnergyPeak_ = 0.0f;  // running peak of energyEnv_ since noteOn
```

### New setter (add after `setNoiseLayerGain`, drum_voice.h:1082)

```cpp
/// Wire coupling: 0 = independent buzz (legacy), 1 = buzz amplitude fully
/// tracks the body's modal-energy envelope. Uses the same gain-invariant
/// getModalEnergy() follower as tension mod; block-rate.
void setWireCoupling(float v) noexcept { wireCoupling_ = std::clamp(v, 0.0f, 1.0f); }
```

### New private helper (add near `effectiveCouplingStrength()`, drum_voice.h:1414)

```cpp
/// Block-rate wire-buzz gain modulation from the modal-energy envelope.
/// energy ~ amplitude^2, so sqrt(energyNorm) tracks the head's AMPLITUDE
/// envelope. Self-normalising: wireEnergyPeak_ is the running peak since
/// noteOn, so the ratio is velocity- and gain-staging-independent.
/// Guards: pre-strike (peak ~ 0) and non-modal bodies (String: modal
/// energy stays 0) both return 1.0 => coupling silently bypasses.
[[nodiscard]] float computeWireGainMod() const noexcept
{
    if (wireCoupling_ <= 1e-6f)
        return 1.0f;
    if (wireEnergyPeak_ <= 1e-12f)
        return 1.0f;
    const float norm  = std::clamp(energyEnv_ / wireEnergyPeak_, 0.0f, 1.0f);
    const float track = std::sqrt(norm);
    return 1.0f + wireCoupling_ * (track - 1.0f);   // lerp(1, track, coupling)
}
```

### Lifecycle resets

- `noteOn()` — next to `energyEnv_ = 0.0f;` (drum_voice.h:380):
  ```cpp
  wireEnergyPeak_ = 0.0f;
  wireGainMod_    = 1.0f;
  ```
- `resetForKitSwitch()` (drum_voice.h:439, next to `energyEnv_ = 0.0f;`): same two lines.

### Energy-follower gating — FAST path (`processBlockFast`)

The follower currently runs only when tension mod is active. Extend the gate:

1. Where `tensionActive` is computed (drum_voice.h:668-674), add below it:
   ```cpp
   const bool wireActive = wireCoupling_ > 1e-6f;
   ```
2. The end-of-chunk follower block (drum_voice.h:848-853) changes from
   `if (tensionActive)` to:
   ```cpp
   if (tensionActive || wireActive)
   {
       const float e = bodyBank_.getSharedBank().getModalEnergy();
       const float a = std::pow(energyAlpha_, static_cast<float>(chunk));
       energyEnv_ = a * energyEnv_ + (1.0f - a) * e;
       if (wireActive)
       {
           wireEnergyPeak_ = std::max(wireEnergyPeak_, energyEnv_);
           wireGainMod_    = computeWireGainMod();
       }
   }
   ```
   NOTE: `tensionPitchMod` (line 671) stays gated on `tensionActive` ALONE — do not
   let `wireActive` enable the pitch glide.
3. Apply the modulation where the noise scratch gain is computed (drum_voice.h:799):
   ```cpp
   const float gain = noiseLayer_.standaloneGain() * noiseLayerGain_ * wireGainMod_;
   ```
   One-chunk latency (gain computed from the previous chunk's energy) is intentional
   and matches how tension mod already behaves. Do NOT recompute per sample.

### Energy-follower gating — SLOW path (`processBlockSlow`)

Mirror the fast path exactly (the two paths MUST stay behavior-equivalent):

1. Next to the slow path's `tensionActive` (drum_voice.h:878-884), add the same
   `wireActive` bool.
2. End-of-block follower (drum_voice.h:999-1008): same `tensionActive || wireActive`
   gate + peak/mod update as the fast path.
3. Apply at the per-sample noise line (drum_voice.h:981):
   ```cpp
   const float noiseSample =
       noiseLayer_.processSample() * noiseLayer_.standaloneGain() * noiseLayerGain_ * wireGainMod_;
   ```

### Per-sample `process()` path (drum_voice.h:495)

Multiply by `wireGainMod_` there too for consistency:
```cpp
const float noiseSample =
    noiseLayer_.processSample() * noiseLayer_.standaloneGain() * noiseLayerGain_ * wireGainMod_;
```
The per-sample `process()` path never runs the energy follower (same as tension mod
and the secondary shell bank, which also don't run there), so `wireGainMod_` stays 1.0
on that path. This is pre-existing convention — document with a one-line comment,
do not "fix" it.

### RT-safety notes (constitution)

- No allocation, no locks: only float math + one `getModalEnergy()` read (already
  called for tension mod). Block-rate, zero per-sample cost when coupling == 0.
- `std::sqrt`/`std::clamp`/`std::pow` already used on these paths.

---

## Plumbing (mirror the M-9 `kPadPan` pattern exactly)

`noiseLayerGain` (state-only, no param) is NOT the model here — `wireCoupling` is a
full host-automatable parameter like pan. Trace of every pan touch point was verified;
replicate each:

### 1. `plugins/membrum/src/dsp/pad_config.h`

- Enum (after `kPadPan = 64`, line 169-170):
  ```cpp
  // Wire coupling: noise-layer buzz tracks the body's modal energy.
  kPadWireCoupling          = 65,
  kPadActiveParamCountV12   = 66,  // offsets 0-65 are active after wire-coupling
  ```
- `padOffsetFromParamId` (line 347): change `kPadActiveParamCountV11` →
  `kPadActiveParamCountV12`.
- `PadConfig` struct — add after `noiseLayerGain` (line 255):
  ```cpp
  // Wire coupling: 0 = independent buzz envelope (legacy), 1 = buzz tracks
  // the body's modal-energy envelope (snare wires driven by head motion).
  float wireCoupling         = 0.0f;
  ```

### 2. `plugins/membrum/src/plugin_ids.h`

- New proxy ID after `kPadPanId = 325` (line 181):
  ```cpp
  // Wire coupling: selected-pad proxy for kPadWireCoupling (offset 65).
  // RangeParameter [0, 1], default 0. Buzz-follows-body depth.
  kWireCouplingId               = 326,
  ```
- Bump `kCurrentStateVersion` 4 → 5 (line 32) and extend the comment
  (`wire-coupling bumped 4 -> 5 for the per-pad wireCoupling slot (58 -> 59)`).
- Update the pin assert (line 221-222):
  ```cpp
  static_assert(kCurrentStateVersion == 5,
                "Pre-release codec is pinned at state version 5 (wire-coupling)");
  ```
- Add collision guard next to the kPadPanId one (line ~257):
  ```cpp
  static_assert(kWireCouplingId < kPadBaseId,
                "Wire-coupling proxy ID must not collide with per-pad range");
  ```

### 3. `plugins/membrum/src/controller/controller.cpp`

- Proxy table (after line 148):
  ```cpp
  // Wire coupling (offset 65).
  {.globalId = kWireCouplingId,               .padOffset = kPadWireCoupling },
  ```
- `kPadParamSpecs` (after line 241):
  ```cpp
  // Wire coupling (offset 65). 0 = legacy independent buzz.
  {.offset = kPadWireCoupling,         .name = "Wire Coupling",       .isDiscrete = false, .stepCount = 0, .defaultValue = 0.0 },
  ```
- static_assert (line 247-248): `kPadActiveParamCountV11` → `kPadActiveParamCountV12`
  (and fix the message: 66 params).
- Register the proxy in `initialize()` next to the Pan registration (~line 654):
  ```cpp
  parameters.addParameter(
      new RangeParameter(STR16("Wire Coupling"), kWireCouplingId, nullptr,
                         0.0, 1.0, 0.0, 0, ParameterInfo::kCanAutomate));
  ```
  (Read the Pan registration's exact arguments/flags first and copy them.)

### 4. `plugins/membrum/src/controller/controller_state_codec.cpp`

- `kLateSlots` (line 58): array size 23 → 24; append:
  ```cpp
  // Wire coupling (sound[58] -> offset 65).
  {.offset = kPadWireCoupling,           .snapIndex = 58},
  ```

### 5. `plugins/membrum/src/processor/processor.cpp`

- After the `kPadPanId` case (line 538-540):
  ```cpp
  // ---- Wire coupling ----
  case kWireCouplingId:
      voicePool_.setPadConfigField(selectedPadIndex_, kPadWireCoupling, fValue);
      break;
  ```

### 6. `plugins/membrum/src/voice_pool/voice_pool.cpp`

- `setPadConfigField` switch (near line 647, `case kPadPan`):
  ```cpp
  case kPadWireCoupling:        cfg.wireCoupling = normalizedValue; break;
  ```
- `applyPadConfig` (near line 789, next to `v.setNoiseLayerGain(...)`):
  ```cpp
  v.setWireCoupling(cfg.wireCoupling);
  ```
  (Read the surrounding function first — if there are TWO config-apply sites, cover both.)

### 7. State codec — `plugins/membrum/src/state/state_codec.h` / `.cpp`

`.h`:
- Both `sound` arrays 58 → 59 (lines 59 and 99); extend the layout doc comments
  (`index 58 -> wireCoupling (buzz-follows-body depth) [wire-coupling]`).
- `kBlobVersion` and `kPadBlobVersion` 4 → 5 (lines 107-108), comments updated.

`.cpp`:
- `toPadSnapshot` (after line 147):
  ```cpp
  // Wire coupling: buzz-follows-body depth.
  snap.sound[58] = static_cast<double>(cfg.wireCoupling);
  ```
- `applyPadSnapshot` (after line 241):
  ```cpp
  cfg.wireCoupling          = std::clamp(static_cast<float>(snap.sound[58]), 0.0f, 1.0f);
  ```
- `applyPadPresetSnapshot` (after line 322 — pad presets DO carry it; it is sound
  character, like noiseLayerGain):
  ```cpp
  cfg.wireCoupling         = std::clamp(static_cast<float>(snap.sound[58]), 0.0f, 1.0f);
  ```
- `toPadPresetSnapshot` uses `copy_n(full.sound.begin(), snap.sound.size(), ...)` —
  picks up slot 58 automatically once both arrays are 59. Verify, don't assume.

**Strict versioning:** v4 blobs are REJECTED on load (pre-release, no migration —
same policy as #255). Factory presets must be regenerated (below).

### 8. UI — `plugins/membrum/resources/editor.uidesc`

Small change → direct string edits OK (XSLT rule applies to substantial restructuring).

- Control tag (after line 99):
  ```xml
  <control-tag name="WireCoupling" tag="326"/>
  ```
- The "Parallel Layers" noise row ends at x=148 (5 knobs, 28 px wide, 34 px pitch);
  the Click group starts at x=200. A 6th knob at x=182 would overlap Click. So:
  1. Shift the Click group right by 34 px: Click section label 200→234; ClickLayerMix
     knob+label 200→234; ClickLayerContactMs 234→268; ClickLayerBrightness 268→302
     (lines 314-320). Container is 388 wide; 302+28=330 fits.
  2. Add after the NoiseLayerColor pair (line 312-313):
     ```xml
     <view class="ArcKnob" control-tag="WireCoupling"      origin="182, 566" size="28, 28" arc-color="accent" arc-width="2.5"/>
     <view class="CTextLabel" title="Wire"                  origin="182, 596" size="28, 10" font="label-font" font-color="text-primary" transparent="true" horizontal-alignment="center"/>
     ```
  3. Search the whole uidesc for OTHER templates referencing the Click tags (there
     may be a second UI mode) — apply the same shift wherever the same row exists.

### 9. Default kit — `plugins/membrum/src/dsp/default_kit.h`

Snare template (after `cfg.noiseLayerGain = 6.2f;`, line 118):
```cpp
// Wire coupling: buzz partially tracks the head so it dies with the body
// and chokes on note-off, instead of running its full fixed 600 ms ADSR.
cfg.wireCoupling         = 0.45f;
```
Leave every other template at the 0.0 default (kick/hat/tom buzz is head noise, not
wires; electronic snares keep machine-like fixed buzz).

### 10. Preset generator — `tools/membrum_preset_generator.cpp`

- `kVersion` 4 → 5 (line 41), comment updated.
- Seed struct: add `double wireCoupling = 0.0;` next to `noiseLayerGain` (line 122).
- Encode list: add `p.wireCoupling,` in the same position as the codec order —
  find line 286 (`p.noiseLayerGain,`) and append after it. READ the surrounding
  function to confirm it maps seed fields → sound[] indices in codec order; slot 58
  must be wireCoupling.
- Acoustic snare pads (all three kits): after each `noiseLayerGain = 6.2` line
  (lines 581, 2022, 2299): `pads[2].wireCoupling = 0.45;`
- Do NOT touch electronic (808/909/LinnDrum/trap/modular), experimental, orchestral,
  or brush snares — same "untouched by design" list as #255.

---

## Tests

### New file: `plugins/membrum/tests/unit/processor/test_wire_coupling.cpp`

Register it wherever `test_snare_body_balance.cpp` is registered (check
`plugins/membrum/tests/CMakeLists.txt` — if sources are globbed, no edit needed;
if listed, add it). Model the harness on `test_snare_body_balance.cpp` (renders a
DrumVoice/VoicePool and measures band RMS) — READ that file first and reuse its
helpers/style. Behavioral cases (write these FIRST, verify they fail, then implement):

1. **`wireCoupling = 0 is bit-exact legacy`** — two voices, identical snare config,
   one with `setWireCoupling(0.0f)` explicitly, one untouched; render 1 s of
   `processBlock` in 512-sample blocks; REQUIRE bit-identical buffers. Then compare
   a coupling=0 render against the same render from a voice where coupling was never
   set — also identical.
2. **`coupling = 1 buzz tracks body decay`** — snare config (default_kit Snare
   template), noteOn(1.0), render 1 s. Measure noise-band RMS (e.g. 4–8 kHz, or reuse
   the balance test's wire-band measure) in a late window (400–600 ms) for coupling 0
   vs coupling 1. REQUIRE coupling-1 late-window wire RMS at least ~12 dB below
   coupling-0 (body b1≈30 s⁻¹ ⇒ head amplitude −60 dB well before 400 ms; buzz ADSR
   alone still ~alive at 600 ms decay norm 0.55). Calibrate the exact threshold
   against the measured render — no magic numbers without a comment.
3. **`note-off chokes the buzz`** — coupling 1, noteOn, 100 ms, noteOff (which damps
   mode radii ×0.997 ⇒ modal energy collapses); REQUIRE wire-band RMS in 250–400 ms
   window far below the same window with coupling 0.
4. **`onset is not attenuated`** — coupling 1: the first 20 ms of output must NOT be
   quieter than coupling 0 by more than ~1 dB (running-peak normalisation ⇒ ratio ≈ 1
   while energy is still rising).
5. **`fast/slow path equivalence`** — same config + coupling 0.7, render via the fast
   path (default) and force the slow path (FeedbackExciter forces slow, but that
   changes the sound — instead check how existing equivalence tests do it; if none
   force slow directly, call `process()`-per-sample vs `processBlock` is NOT the pair
   to compare (per-sample path legitimately holds mod at 1.0). Acceptable fallback:
   two `processBlock` renders with different block sizes (64 vs 512) must match within
   a small tolerance — this catches chunk-rate bugs in the mod update.)
6. **`String body bypasses coupling`** — body String, coupling 1: output identical to
   coupling 0 (modal energy stays 0 ⇒ guard returns 1.0).
7. **State round-trip** — set `cfg.wireCoupling = 0.37f`, `toPadSnapshot` →
   `applyPadSnapshot`, REQUIRE 0.37f. Same through the pad-preset snapshot pair.

### Pinned tests to update (they WILL fail after the version bump — fix all, own all)

| File | Change |
|---|---|
| `tests/unit/controller/test_phase6_parameters.cpp:37` | version pin 4 → 5, section name text |
| `tests/unit/preset/test_pad_preset.cpp:40` | `kPadPresetSoundParamCount` 58 → 59 + comment |
| `tests/unit/controller/test_ui_mode_session_scope.cpp:169` | expected blob size +256 bytes (32 pads × 1 slot × 8 bytes); read surrounding math and update the constant + comment |
| `tests/unit/processor/test_state_v6_migration.cpp:191` | per-pad block math: 58×8=464 → 59×8=472 (474 → 482 per pad); update expression + comment |
| `tests/unit/processor/test_default_kit.cpp:125` | add `REQUIRE(snare.wireCoupling == Approx(0.45f).margin(0.01f));` |
| `tests/unit/vst/test_pad_config.cpp:241` | add checks for offset 65 valid / 66 invalid |
| `tests/unit/state/test_state_codec.cpp` | version-rejection tests are parameterised on kBlobVersion — verify they still pass, update any hardcoded `4` |

Also grep tests for `kPadActiveParamCountV11`, `= 58`, `== 4` near state/blob code —
any other pins must be found and updated, not discovered by CI.

### Golden files

`git show a1480c6b --stat` shows `phase1_default.bin` was regenerated for the v4 bump.
Grep the repo for `phase1_default.bin` to find the regen procedure (there is a
generator test or WRITE mode); regenerate for v5 the same way. Any state-format golden
that embeds a serialized kit must be regenerated — find them all
(`grep -rl "\.bin" plugins/membrum/tests`).

---

## Factory presets

1. Build + run the preset generator (find its CMake target: grep `membrum_preset_generator`
   in CMakeLists files). It now emits v5 blobs.
2. Regenerate ALL factory `.vstpreset` (all categories — v4 files are rejected by the
   v5 codec, so every preset must be re-emitted even if its values didn't change).
   Follow the file list from `git show a1480c6b --stat` for where generated presets
   live in the repo.
3. Copy regenerated presets to `C:\ProgramData\Krate Audio\Membrum\Kits\{category}\`
   (they live there, NOT in the VST3 bundle). Categories are FIXED:
   Acoustic, Electronic, Percussive, Unnatural.
4. Install the plugin via the build target only — NEVER hand-copy a single VST3 file.

---

## CHANGELOG

Add to `plugins/membrum/CHANGELOG.md` under an Unreleased/next section (match existing
style): feature description (wire coupling, physics rationale, default 0 = legacy),
state format v4 → v5 (strict, no migration), factory presets regenerated, acoustic
snares set to 0.45. No version.json bump unless the user asks for a release.

---

## Execution order (canonical todo list)

1. Write `test_wire_coupling.cpp` cases 1–4, 6, 7 (they fail: no `setWireCoupling`,
   no field). Confirm compile failure / test failure BEFORE implementing.
2. Implement DSP core (drum_voice.h) + PadConfig field. Build, run new tests.
3. Plumbing: pad_config offsets, plugin_ids, controller, controller_state_codec,
   processor, voice_pool. Build.
4. State codec v5 (+ generator kVersion). Build.
5. Fix all pinned tests + regen goldens. Build; run **full** `membrum_tests`.
6. uidesc edit; default_kit + generator preset values; regenerate + install presets.
7. Full verification (below), then commit.

**Build/test gates between every step. NO TESTS WITHOUT A CLEAN BUILD. Zero warnings.**

Commands (bash; CMake needs full path on Windows):
```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target membrum_tests 2>&1 | tail -20
build/windows-x64-release/bin/Release/membrum_tests.exe 2>&1 | tail -5
# targeted run while iterating:
build/windows-x64-release/bin/Release/membrum_tests.exe "*wire*" 2>&1 | tail -5
```
Run each slow tool ONCE, redirect to a log file, inspect the log — never re-run to grep.

## Final verification checklist

- [ ] `membrum_tests.exe` — ALL pass (last line `All tests passed`), full suite once.
- [ ] Full plugin build: `--target Membrum` clean, zero warnings (post-build copy
      permission failure to Program Files is expected/OK).
- [ ] `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3"` → log file, PASSED.
- [ ] clang-tidy: `./tools/run-clang-tidy.ps1 -Target membrum -BuildDir build/windows-ninja`
      → log file, zero NEW warnings; fix all warnings in touched files.
- [ ] Audition (optional but recommended): extend `tests/unit/diagnose/test_snare_audition.cpp`
      to render coupling 0 / 0.45 / 1.0 WAVs for the user to A/B.
- [ ] Commit (new commit, NEVER `--amend`; do not push).

## Do-nots / pitfalls

- Do NOT add a `parameters/` dir — Membrum pipeline is
  `plugin_ids.h → processor → controller → editor.uidesc`.
- Do NOT gate the tension pitch glide on `wireActive` — pitch mod stays
  Membrane+tension only.
- Do NOT recompute `wireGainMod_` per sample — block-rate only, matching tension mod.
- Do NOT let the follower run when BOTH tension and wire are inactive (keeps the
  default path zero-overhead — existing contract).
- Do NOT change `noiseLayerGain` semantics or the NoiseLayer class itself — the
  modulation lives entirely in DrumVoice at the three mix sites.
- Do NOT invent new preset categories or hand-copy VST3 bundle files.
- Do NOT dismiss any failing test as pre-existing — fix everything the suite reports.
- Fast and slow block paths MUST stay behavior-equivalent — every edit to one gets
  the mirror edit in the other.
- All three noise-mix sites get the multiplier: fast (l.799), slow (l.981),
  per-sample `process()` (l.495) — line numbers are pre-change anchors; re-locate
  by content, they shift as you edit.
