## Implementation Plan — Make Membrum's Hand Clap Sound Like a Hand Clap (REVISED)

All file paths are absolute. Every line number below was re-verified by reading the actual files in the repo at `f:\projects\iterum` on 2026-07-16. **Revision note:** this version fixes two count/OOB test blockers, one self-defeating test instruction, two acoustics majors (burst resolvability + residual modal pitch), and corrects the preset-compatibility proof and several stale references that the previous plan got wrong.

---

## 1. Design decision (ONE approach — do not deviate)

**Add a new one-shot multi-burst bandpassed-noise exciter `ClapExciter` (`ExciterType::Clap = 6`) and a new `DrumTemplate::Clap` that pairs it with the existing `BodyModelType::NoiseBody` plus a decaying parallel `NoiseLayer` "room" tail.**

Justification (grounded in verified code):
- The exciter is the ONLY stage that models excitation, and a hand clap IS a multi-impulse excitation — 3-4 rapid broadband noise bursts (the TR-808/909 architecture). The current pad uses a single soft `Mallet` strike into a `Plate` modal bank whose pitched ring reads as the user's "copper triangle." A multi-burst exciter reproduces the "several hands not quite in sync" flam the ear labels as a clap.
- The exciter's output reaches the output ONLY THROUGH the body (verified `f:\projects\iterum\plugins\membrum\src\dsp\drum_voice.h:522-524`: `excMain = exciterBank_.process(bodyBank_.getLastOutput())` → `body = bodyBank_.processSample(exc)`), so the body must be broadband/non-pitched. `NoiseBody` is the existing dense-mode-wash body already used by Hat/Cymbal. There is no body-bypass, so `NoiseBody` is the correct pairing.
- The diffuse ~150-300 ms room tail is supplied by the always-on parallel `NoiseLayer`, which sums into the body output before the UnnaturalZone chain (verified `drum_voice.h:536-539`: `noiseSample = noiseLayer_.processSample() * noiseLayer_.standaloneGain() * noiseLayerGain_ * wireGainMod_`).

This is template-level (a new `DrumTemplate` archetype whose `PadConfig` fields are all already plumbed) plus ONE new exciter header. **No new `PadConfig` field is introduced**, so there is NO new state-codec / processor-param / controller-registration / uidesc plumbing. The only cross-cutting change is bumping the `ExciterType` enum and appending one string to the exciter dropdown — and every processor/state/controller site computes ranges dynamically from `ExciterType::kCount`, so they auto-adapt.

**⚠ CRITICAL — the enum bump breaks TWO tests that DO NOT auto-adapt (both must be fixed, see Steps 6a/6b):**
1. `plugins\membrum\tests\unit\test_exciter_body_matrix.cpp:479-480` asserts `CHECK(tested == 144)` / `CHECK(passed == 144)`. This is a **non-`[.perf]` test that runs in the default `membrum_tests.exe` run.** At `kCount == 7` the loop runs 7×6×2×2 = **168** combinations, so `tested == 144` FAILS. Must become 168.
2. `plugins\membrum\tests\perf\test_benchmark_144.cpp:292` declares a fixed-size `std::array<BenchResult, 144>` and writes `results[resultIdx++]` for all 7×6×2×2 = 168 iterations → **out-of-bounds write** of indices 144..167. `[.perf]`-tagged (skipped by default) but a real OOB that fires under the valgrind-linux CI lane and any explicit perf run. Must be resized.

**Preset compatibility (CORRECTED proof — the previous plan's `floor((i/oldCount)*newCount)` formula does not exist in the code):**
The authoritative persistent representation is a **raw `int32` enum index** written to the processor state chunk. Verified at `f:\projects\iterum\plugins\membrum\src\state\state_codec.cpp:445-454`: on load, `readT(streamer, excI)` reads the int32 and `excI = std::clamp(excI, 0, kCount-1)`. Appending `Clap = 6` at the END of the enum never remaps existing indices 0-5, so any saved preset keeps its exciter selection.
The controller-side normalized bridge is internally self-consistent within a single `kCount`: encode is `(index + 0.5) / kCount` (verified `f:\projects\iterum\plugins\membrum\src\controller\controller_state_codec.cpp:190-192`) and decode is `clamp(int(norm * kCount), 0, kCount-1)` (verified `controller_state_codec.cpp:124-126`). Both use the CURRENT `kCount`, so the round-trip lands on the integer regardless of the count value. There is no cross-count normalized migration path, so no compatibility concern. **Do NOT reason from any `floor(i*newCount/oldCount)` claim — it is not the mechanism.**

---

## 2. Exact code changes (ordered)

> **Test-first note (project mandate):** Author the tests in Section 5 FIRST and run them to confirm they FAIL against the current Perc-based pad 3 (test 1 in particular: the Perc pad produces exactly one onset). Only then make the Step 1-6 changes and re-run to green. See Section 5 for the explicit sequence.

### Step 1 — Add the `Clap` enum value
**File:** `f:\projects\iterum\plugins\membrum\src\dsp\exciter_type.h`

Current (verified, lines 9-21):
```cpp
enum class ExciterType : int
{
    Impulse    = 0,
    Mallet     = 1,
    NoiseBurst = 2,
    Friction   = 3,
    FMImpulse  = 4,
    Feedback   = 5,
    kCount     = 6,
};

static_assert(static_cast<int>(ExciterType::kCount) == 6,
              "ExciterType::kCount must reflect the number of alternatives");
```
Change to:
```cpp
enum class ExciterType : int
{
    Impulse    = 0,
    Mallet     = 1,
    NoiseBurst = 2,
    Friction   = 3,
    FMImpulse  = 4,
    Feedback   = 5,
    Clap       = 6,
    kCount     = 7,
};

static_assert(static_cast<int>(ExciterType::kCount) == 7,
              "ExciterType::kCount must reflect the number of alternatives");
```

### Step 2 — Create the `ClapExciter` header
**File (new):** `f:\projects\iterum\plugins\membrum\src\dsp\exciters\clap_exciter.h`

Model it on the sibling `f:\projects\iterum\plugins\membrum\src\dsp\exciters\noise_burst_exciter.h` (same `NoiseOscillator → envelope → SVF Bandpass` structure and the same public interface the `ExciterBank` visitors call: `prepare(sr,voiceId)`, `reset()`, `trigger(vel)`, `release()`, `process(bodyFeedback)`, `isActive()`). The difference is a schedule of **4 overlapping bursts** instead of one. **Before writing, open `noise_burst_exciter.h` and copy the EXACT method/enum names** for `Krate::DSP::NoiseOscillator` (`prepare`/`setColor`/`setSeed`/`reset`/`process`), `Krate::DSP::SVF` (`prepare`/`setMode`/`setResonance`/`setCutoff`/`snapToTarget`/`reset`/`process`), `Krate::DSP::NoiseColor::White`, and `Krate::DSP::SVFMode::Bandpass` — do not guess them.

Write exactly:

```cpp
#pragma once

// ==============================================================================
// ClapExciter -- multi-burst broadband noise excitation for a hand clap.
// ==============================================================================
// A hand clap read as "clap" (not "mallet/metal") requires an aperiodic
// multi-impulse excitation: 3-4 rapid broadband noise bursts spaced ~8-12 ms
// apart (the "several hands not quite in sync" flam), followed by a diffuse
// room tail. This exciter models the burst TRAIN (the classic TR-808/909 comb
// re-trigger); the diffuse tail is supplied downstream by the voice's parallel
// NoiseLayer. Signal path per sample:
//   NoiseOscillator (White) -> summed 4-burst raised-cosine/exp envelope -> SVF Bandpass
//
// Burst timing is FIXED (velocity-independent) so the "clap" rhythm is stable
// across dynamics; velocity scales amplitude and shifts the bandpass center for
// the harder-hit "sharper crack". Onsets carry a mild non-uniform jitter
// (spacings 10/11/12 ms) so the train is aperiodic, not a buzzy comb.
//
// NOTE (SC-004 exemption): this exciter intentionally uses a NARROW velocity->
// brightness sweep (stable clap character across dynamics). It is therefore
// deliberately exempt from the SC-004 "centroid ratio >= 2.0" per-exciter test
// (test_velocity_mapping.cpp) and MUST NOT be added to that test's list.
//
// Contract (matches NoiseBurstExciter):
//   - process() returns 0.0 once the last burst has decayed
//   - all state pre-allocated in prepare(); trigger() allocates nothing
//   - retrigger-safe: trigger() re-seeds/resets so a stolen/choked voice renders
//     bit-identically to a pristine voice (FR-124)
// ==============================================================================

#include <krate/dsp/primitives/noise_oscillator.h>
#include <krate/dsp/primitives/svf.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Membrum {

struct ClapExciter
{
    // 4 bursts: onsets 0/10/21/33 ms (spacings 10,11,12 ms -> aperiodic, in the
    // 8-12 ms target band); relative amplitudes taper ~0/-1/-1.7/-2.5 dB.
    static constexpr int   kNumBursts     = 4;
    static constexpr std::array<float, kNumBursts> kOnsetMs = {0.0f, 10.0f, 21.0f, 33.0f};
    static constexpr std::array<float, kNumBursts> kRelAmp  = {1.0f, 0.90f, 0.82f, 0.75f};
    static constexpr float kBurstDecayMs  = 8.0f;   // per-burst -60 dB span
    static constexpr float kBurstRiseMs   = 0.6f;   // fast attack (<1 ms target)

    void prepare(double sampleRate, std::uint32_t voiceId) noexcept
    {
        sampleRate_ = sampleRate;
        noise_.prepare(sampleRate);
        // White noise: warm TR-808 flavour (NOT the +6 dB/oct Violet the hi-hat
        // NoiseBurst uses, which would read as too hissy for a clap).
        noise_.setColor(Krate::DSP::NoiseColor::White);
        noise_.setSeed(0xC1A9u ^ voiceId);
        filter_.prepare(sampleRate);
        filter_.setMode(Krate::DSP::SVFMode::Bandpass);
        filter_.setResonance(1.0f);   // gentle single hump (Q~1); NOT ringing
        filter_.setCutoff(1600.0f);
        filter_.snapToTarget();
        reset();
    }

    void reset() noexcept
    {
        noise_.reset();
        filter_.reset();
        sampleIdx_    = 0;
        totalSamples_ = 0;
        riseSamples_  = 1;
        decaySamples_ = 1;
        decayK_       = 1.0f;
        amplitude_    = 0.0f;
        for (int k = 0; k < kNumBursts; ++k)
            onsetSamples_[static_cast<std::size_t>(k)] = 0;
        active_ = false;
    }

    void trigger(float velocity) noexcept
    {
        // Re-seed + reset for deterministic, bit-identical retrigger (FR-124).
        noise_.reset();
        filter_.reset();

        velocity = std::clamp(velocity, 0.0f, 1.0f);
        const float sr = static_cast<float>(sampleRate_);

        riseSamples_  = std::max(1, static_cast<int>(std::lround(kBurstRiseMs  * 1.0e-3f * sr)));
        decaySamples_ = std::max(1, static_cast<int>(std::lround(kBurstDecayMs * 1.0e-3f * sr)));
        decayK_       = 6.9078f / static_cast<float>(decaySamples_);  // ~-60 dB
        const int burstSpan = riseSamples_ + decaySamples_;

        int lastEnd = 0;
        for (int k = 0; k < kNumBursts; ++k)
        {
            onsetSamples_[static_cast<std::size_t>(k)] =
                static_cast<int>(std::lround(kOnsetMs[static_cast<std::size_t>(k)] * 1.0e-3f * sr));
            lastEnd = std::max(lastEnd, onsetSamples_[static_cast<std::size_t>(k)] + burstSpan);
        }
        totalSamples_ = lastEnd;

        // Velocity: overall amplitude 0.2 (soft) -> 1.0 (hard).
        amplitude_ = 0.2f + 0.8f * velocity;

        // Brightness rises with velocity: bandpass center 1200 -> 2200 Hz. (Real
        // 909/hand-clap spectra peak ~1.5-2.5 kHz -- a darker band reads as a
        // muffled thud and drags the whole-render centroid below the test-3 gate.)
        const float cutoff = 1200.0f + 1000.0f * velocity;
        filter_.setCutoff(cutoff);
        filter_.snapToTarget();

        sampleIdx_ = 0;
        active_    = true;
    }

    void release() noexcept
    {
        // One-shot burst train; release is a no-op (the train finishes on its own).
    }

    [[nodiscard]] float process(float /*bodyFeedback*/) noexcept
    {
        if (sampleIdx_ >= totalSamples_)
        {
            active_ = false;
            return 0.0f;
        }

        // Summed multi-burst envelope: each burst contributes a raised-cosine
        // rise then an exponential decay, gated to its own [onset, onset+span).
        float env = 0.0f;
        for (int k = 0; k < kNumBursts; ++k)
        {
            const int n = sampleIdx_ - onsetSamples_[static_cast<std::size_t>(k)];
            if (n < 0 || n >= riseSamples_ + decaySamples_)
                continue;
            float e;
            if (n < riseSamples_)
            {
                const float t = static_cast<float>(n) / static_cast<float>(riseSamples_);
                e = 0.5f * (1.0f - std::cos(kPi * t));
            }
            else
            {
                e = std::exp(-decayK_ * static_cast<float>(n - riseSamples_));
            }
            env += kRelAmp[static_cast<std::size_t>(k)] * e;
        }

        const float raw = noise_.process() * amplitude_ * env;
        const float out = filter_.process(raw);
        ++sampleIdx_;
        return out;
    }

    [[nodiscard]] bool isActive() const noexcept
    {
        return active_ && sampleIdx_ < totalSamples_;
    }

private:
    static constexpr float kPi = 3.14159265358979323846f;

    Krate::DSP::NoiseOscillator noise_{};
    Krate::DSP::SVF             filter_{};
    double sampleRate_   = 44100.0;
    int    sampleIdx_    = 0;
    int    totalSamples_ = 0;
    int    riseSamples_  = 1;
    int    decaySamples_ = 1;
    float  decayK_       = 1.0f;
    float  amplitude_    = 0.0f;
    std::array<int, kNumBursts> onsetSamples_{};
    bool   active_       = false;
};

} // namespace Membrum
```

> Note: `static constexpr std::array` members are implicitly inline in C++20 (ODR-safe). If a compiler still complains about linkage, move the `kOnsetMs`/`kRelAmp` arrays to `static constexpr` LOCAL variables inside `trigger()` (and re-add a local copy inside `process()`), keeping identical values.

### Step 3 — Register `ClapExciter` in the ExciterBank variant
**File:** `f:\projects\iterum\plugins\membrum\src\dsp\exciter_bank.h`

3a. Add the include next to the other `#include "exciters/..."` lines near the top of the file:
```cpp
#include "exciters/clap_exciter.h"
```
3b. Add `ClapExciter` to the `Variant` alias (verified currently lines 169-175):
```cpp
    using Variant = std::variant<
        ImpulseExciter,
        MalletExciter,
        NoiseBurstExciter,
        FrictionExciter,
        FMImpulseExciter,
        FeedbackExciter,
        ClapExciter>;
```
3c. Add the emplace case in `applyPendingSwap()` — insert BEFORE `case ExciterType::kCount:` (verified currently line 201). The switch has NO `default:`, so this keeps it exhaustive:
```cpp
        case ExciterType::Clap:
            active_.emplace<ClapExciter>();
            break;
```
No secondary-param replay is needed for `ClapExciter` (no host-facing secondary knob), so leave the `holds_alternative<...>` replay block (verified lines 216-225) unchanged.

### Step 4 — Add the `Clap` template archetype
**File:** `f:\projects\iterum\plugins\membrum\src\dsp\default_kit.h`

4a. Add `Clap` to the `DrumTemplate` enum (verified currently around lines 22-30 — enum lists `Kick, Snare, Tom, Hat, Cymbal, Perc`):
```cpp
enum class DrumTemplate
{
    Kick,
    Snare,
    Tom,
    Hat,
    Cymbal,
    Perc,
    Clap,
};
```
4b. Add a `case DrumTemplate::Clap:` to `applyTemplate()` — insert immediately AFTER the `case DrumTemplate::Perc:` block's `break;` (verified that block ends at line 292, before the switch's closing `}` at line 293). Use these STARTING values (they will be tuned in the Section 6 loop — see the resolvability note there):
```cpp
        case DrumTemplate::Clap:
            // Hand-clap (HAND-CLAP-PLAN). A clap is NOT a struck resonant body:
            // it is a multi-burst broadband noise flam + a diffuse room tail.
            //   * ClapExciter fires 4 bandpassed noise bursts (~10 ms apart) ->
            //     the "several hands not quite in sync" attack.
            //   * NoiseBody (dense-mode wash) gives broadband colour. NoiseBody
            //     is 0.6*modalBank + noise with a FIXED modalMix=0.6 (see
            //     noise_body_mapper.h:178) and a modal fundamental f0 = 1500 *
            //     0.1^size, so pitch salience is real -- it is killed here with
            //     HEAVY modeScatter + modeStretch (like Cymbal), NOT by relying
            //     on NoiseBody being pitch-free (it is not).
            //   * Short body decay + high damping so the modal ring dies fast and
            //     the 4 bursts stay resolvable (no smearing into one blob).
            //   * The parallel NoiseLayer supplies the ~200 ms diffuse tail (the
            //     808 "reverb"); its gain is tuned AGAINST burst resolvability
            //     (a loud smooth noise floor fills the inter-burst valleys).
            //   * clickLayer is a small, dark crack -- NOT a bright metallic tick.
            //   * No modeInject / coupling (a clap has no pitch and must not ring
            //     undamped).
            cfg.exciterType = ExciterType::Clap;
            cfg.bodyModel   = BodyModelType::NoiseBody;
            cfg.material       = 0.70f;   // moderate broadband brightness
            cfg.size           = 0.25f;
            cfg.decay          = 0.10f;   // short modal ring
            cfg.strikePosition = 0.3f;
            cfg.level          = 0.8f;
            // Fast modal decay so the four bursts do not smear together.
            cfg.bodyDampingB1  = 0.65f;   // high flat damping (short T60)
            cfg.bodyDampingB3  = 0.30f;
            // Diffuse "room" tail: low-passed white noise, ~200 ms decay. Gain is
            // START value; the burst-resolvability test (Section 5 test 1) and the
            // tail-audibility test (test 4) TRADE OFF against this -- tune together.
            cfg.noiseLayerMix        = 0.70f;
            cfg.noiseLayerCutoff     = 0.70f;  // parallel layer is LOWPASS: higher
                                               // cutoff keeps HF/centroid UP.
            cfg.noiseLayerResonance  = 0.20f;
            cfg.noiseLayerDecay      = 0.50f;  // ~200 ms tail
            cfg.noiseLayerColor      = 0.60f;  // white
            cfg.noiseLayerGain       = 1.20f;  // START low; raise only if tail
                                               // inaudible AND test 1 still passes.
            // Small dark crack for the initial contact (not a bright ping).
            cfg.clickLayerMix        = 0.25f;
            cfg.clickLayerContactMs  = 0.15f;
            cfg.clickLayerBrightness = 0.50f;
            // PRIMARY anti-pitch levers (NoiseBody's modalMix is fixed at 0.6 and
            // is NOT a PadConfig field, so these + damping are the only controls):
            cfg.airLoading  = 0.0f;
            cfg.modeScatter = 0.35f;   // heavy scatter -> inharmonic cloud (Cymbal
                                       // uses 0.35 for exactly this reason).
            cfg.modeStretch = 0.60f;   // stretch the plate ratios off their tuned
                                       // grid so no single partial dominates.
            break;
```
> Before writing, confirm `PadConfig` has a `modeStretch` field (it is set on the Cymbal/Hat templates elsewhere in this file and forwarded by `noise_body_mapper.h:118-120`). If the field name differs, match the name used by the existing Cymbal template block.

4c. Point pad 3 at the new template. In the `kSpecs[]` table, change line 353 from:
```cpp
        {.tmpl = DrumTemplate::Perc,   .sizeOverride = -1.0f },  //  3: MIDI 39 Hand Clap
```
to:
```cpp
        {.tmpl = DrumTemplate::Clap,   .sizeOverride = -1.0f },  //  3: MIDI 39 Hand Clap
```
Also update the human-readable GM map comment at `default_kit.h:310` (currently `//  3    39   Hand Clap            Perc       -`) to say `Clap` instead of `Perc`.
**Do NOT touch pads 1 (Side Stick) or 20 (Cowbell)** — out of scope; leave them `Perc`.

### Step 5 — Add "Clap" to the exciter dropdown string list
**File:** `f:\projects\iterum\plugins\membrum\src\controller\controller.cpp`

There are exactly TWO sites that build the exciter string list (verified: lines 303 and 689, both `appendString(STR16("Feedback"))`). Append `"Clap"` after `"Feedback"` in BOTH (order must match the enum: Clap is last, index 6).

5a. After line 303 (`excList->appendString(STR16("Feedback"));`):
```cpp
        excList->appendString(STR16("Clap"));
```
5b. After line 689 (`list->appendString(STR16("Feedback"));`):
```cpp
                list->appendString(STR16("Clap"));
```
The `COptionMenu` in `resources\editor.uidesc` (control-tag `ExciterType`) auto-populates from this `StringListParameter`, so **no uidesc edit is needed**. The step count derives from the number of appended strings (6 → 7). This is the established add-an-exciter pattern.

### Step 6 — Fix the count/OOB sites the enum bump breaks (MANDATORY — these do NOT auto-adapt)

**6a. `f:\projects\iterum\plugins\membrum\tests\unit\test_exciter_body_matrix.cpp` (non-`[.perf]`, runs by default — BLOCKER):**
- Lines 479-480: change the hardcoded totals so they compute from `kCount`. `kNumExciters`/`kNumBodies` are already `constexpr` in that TEST_CASE (lines 410-411):
  ```cpp
      CHECK(tested == kNumExciters * kNumBodies * 4);
      CHECK(passed == kNumExciters * kNumBodies * 4);
  ```
- Add a `case Membrum::ExciterType::Clap: return "Clap";` to the `exciterName()` switch (lines 66-78) so the INFO label is correct (it currently has `default: return "Unknown"`, so this is cosmetic-but-do-it).
- Update the two `"...36 combos..."` TEST_CASE display strings (lines 153, and the sample-rate note text) to `42` where they name the count — cosmetic, no assertion depends on it, but keep it honest.

**6b. `f:\projects\iterum\plugins\membrum\tests\perf\test_benchmark_144.cpp` (`[.perf]`, OOB write — BLOCKER):**
- Line 292: replace the fixed-size array with a `kCount`-derived size. `kNumExciters`/`kNumBodies` are `constexpr` in that TEST_CASE (lines 289-290):
  ```cpp
      std::array<BenchResult, static_cast<std::size_t>(kNumExciters * kNumBodies * 4)> results{};
  ```
- Add `case Membrum::ExciterType::Clap: return "Clap";` to the `exciterName()` helper (line 60+) so CSV/printf output is correct.
- Update the `144` / `6 exciter × 6 body` text in the header comment (lines 1-8) and the TEST_CASE name (line 286) to reflect 168 combos (7 exciter × 6 body × 2 × 2). No assertion depends on the literal.

**6c. `f:\projects\iterum\plugins\membrum\tests\unit\exciters\test_velocity_mapping.cpp` — DO NOT add Clap (BLOCKER if you do):**
This is the SC-004 per-exciter test `CHECK(r.ratio >= 2.0f)` (line 83) over a hardcoded 6-type list (lines 65-72). `ClapExciter`'s narrow velocity→brightness sweep (bandpass 1430 Hz @ vel 0.23 → 2200 Hz @ vel 1.0, ratio ≈ 1.5, further flattened by the fixed White-noise HF content) CANNOT reach 2.0. **Leave this file at the existing 6 exciters.** Clap is intentionally exempt from SC-004 (deliberate "stable clap character" design — documented in the ClapExciter header comment). Do NOT add Clap to its list and do NOT weaken the exciter to chase 2.0.

**6d. Verify (do not assume) all other `kCount`-derived and hardcoded-exciter sites:**
Run these greps over the whole `plugins\membrum` tree (src AND tests):
```
grep -rn "ExciterType" plugins/membrum/tests
grep -rn "case ExciterType::Feedback" plugins/membrum/src
grep -rn "== 144\|== 36\|BenchResult, 144\|tested ==\|passed ==" plugins/membrum/tests
```
- The kCount-driven matrix loops (`test_exciter_body_matrix.cpp:156-157,171-173,410-411,423-425`; `test_allocation_matrix.cpp:116-121`; `test_exciter_bank.cpp:177-178,186-188`) auto-expand to include `Clap × every body`. That is EXPECTED and must pass — `ClapExciter` is allocation-free and BIBO-stable, so these NaN/Inf/bounded/alloc-free assertions hold. Add a `Clap` case to any `exciterName()`/`exName()` switch you find (they have `default:` so it is cosmetic, but keep labels honest).
- If any `case ExciterType::Feedback` switch in `src` lacks a `default:`, add a `case ExciterType::Clap:` arm (the ExciterBank switch in Step 3c is the known one; the grep confirms whether there are others).
- The six per-body tests (`test_string_body.cpp:189`, `test_noise_body.cpp:220`, `test_plate_body.cpp:232`, `test_membrane_body.cpp:149`, `test_bell_body.cpp:147`, `test_shell_body.cpp:177`) use hardcoded 6-element `exciters[]` arrays. They do NOT break compilation and keep testing 6 exciters. Adding `Membrum::ExciterType::Clap` to each array is OPTIONAL extra coverage (Clap × that body) — cheap and recommended, but not a blocker. If you add it, verify each still passes.

---

## 3. Real-time safety notes (per step)

- **Step 2 (ClapExciter):** All DSP state (`NoiseOscillator`, `SVF`, the `onsetSamples_` array) is preallocated in `prepare()`. `trigger()` writes only scalars/fixed-size arrays — NO heap, lock, or exception. `process()` is a bounded 4-iteration loop of arithmetic + one `noise_.process()` + one `filter_.process()`; `std::exp`/`std::cos` are called per active-burst sample only (bounded). Fully audio-thread safe. This is what the auto-expanded `test_allocation_matrix.cpp` (Step 6d) verifies for `Clap × every body`.
- **Step 4 (template):** Data-only field assignments to a `PadConfig`; runs once at boot inside `DefaultKit::apply` (called from the processor init path), never on the audio thread. `NoiseBody` is already an RT-safe body used by Hat/Cymbal.
- **No new atomics/params:** because no new `PadConfig` field is added, there is nothing new to publish across the UI/audio boundary.
- **Strike-normalization probe (verified):** the Clap exciter is driven by the LIVE-exciter probe `probeStrikePeakExcited` (`f:\projects\iterum\plugins\membrum\src\dsp\drum_voice.h:1311-1352`), which triggers `exciterBank_` at velocity 1.0 then `reset()`s it (lines 1327, 1345). `ClapExciter::trigger()` re-seeds/resets deterministically, so the probe leaves no residue and the audible note is bit-identical. The `StrikeNormKey` (struct at `drum_voice.h:1706`; constructed at `drum_voice.h:1389`) keys on `exciter` type, so switching pad 3 to `Clap` correctly re-runs the probe.
  - **⚠ Probe window vs Clap train length:** the probe uses a FIXED 30 ms window (`drum_voice.h:1316-1318`), but the Clap burst train spans onset 33 ms + rise 0.6 ms + decay 8 ms ≈ 42 ms, so the probe observes only ~the first 3 of 4 bursts. This is expected to be BENIGN because burst 1 (relAmp 1.0, amplitude 1.0 at vel 1.0) is the loudest and the body decay is short, so the excited-strike peak falls inside 30 ms. **Confirm this during the Section 6 render loop:** check the clap's peak dBFS is stable/expected across velocities. If the measured peak ever lands in the 4th burst (peak dBFS unexpectedly low or non-monotone), either shorten the train to fit 30 ms or leave a note that the probe window would need widening (out of scope unless it actually misbehaves).

---

## 4. Fast/Slow path

**The change touches both paths only in that both call the new exciter's `process()` per sample — no path-specific branch is introduced, so equivalence is structurally preserved.**
- Path selection is `useSlowPath = feedbackExciter` (verified `f:\projects\iterum\plugins\membrum\src\dsp\drum_voice.h:609`, with `feedbackExciter` computed at line 599). `Clap ≠ Feedback`, so the Clap voice always runs the **FAST path** (`processBlockFast`), which calls the exciter per sample exactly like every other non-feedback exciter.
- The SLOW path (`processBlockSlow`) also calls `exciter.process(lastBody)` per sample, so even if a Clap voice were ever forced onto the slow path, its output is identical (the exciter is a pure per-sample state machine; sample ordering is the same). The single-sample `processSample()` path (`drum_voice.h:522-555`) likewise calls it identically.
- `NoiseBody`, `NoiseLayer`, and `ClickLayer` all already have matched fast (`processBlock`) and slow (`processSample`) implementations exercised by the Hat/Cymbal templates — this plan adds no new per-path logic to any of them.
- **Do NOT** add any Clap-specific special-casing to only one of `processBlockFast`/`processBlockSlow`. If you find yourself editing one, stop — you have gone off-plan.

---

## 5. Tests (Catch2, in `plugins\membrum\tests\`)

**SEQUENCE (test-first mandate):**
1. Write both test files below and register them in CMake.
2. Build `membrum_tests` and run ONLY the new tests. Confirm the acceptance tests FAIL against the current Perc-based pad 3 (test 1 in particular — the Perc pad produces exactly one onset; test 2/3 fail on the pitched 403 Hz ring). Capture that red output.
3. Make the Step 1-6 code changes.
4. Rebuild and re-run — confirm green.

Create **`f:\projects\iterum\plugins\membrum\tests\unit\dsp\test_clap_acceptance.cpp`**, modeled on `plugins\membrum\tests\unit\dsp\test_crash_acceptance.cpp` (copy its `NoteEventList`, `renderMono`, and FFT helpers verbatim). Render the DEFAULT-KIT clap: `renderMono(39, velocity, 0.6)` at `kSampleRate = 48000`, `kBlock = 512`. Thresholds are deliberately generous — they separate "clap" from "copper triangle", not fine-tune:

1. **`CLAP multi-burst count` (velocity 0.9):** Build a rectified, one-pole-smoothed (~1 ms) amplitude envelope of the first 45 ms. Count local maxima that exceed `0.25 * globalPeak` and are separated by ≥ 5 ms. **Assert count ∈ [3, 4].** Assert mean inter-peak spacing ∈ [7 ms, 14 ms]. Assert first-peak 10%-90% rise time < 2 ms. (Decisive anti-single-transient test — current Perc pad = exactly 1 onset.)

2. **`CLAP no discrete partial` (velocity 0.9):** FFT the whole 0.6 s render (Hann, `kFft = 8192`). Over 500 Hz-6 kHz compute spectral flatness (geometric mean / arithmetic mean of the power spectrum). **Assert flatness > 0.15.** Also assert NO single FFT bin in that band exceeds a smoothed (±1/6-octave) local spectral envelope by more than 8 dB. (Guards the copper-triangle failure.)

3. **`CLAP spectral placement` (velocity 0.9):** Whole-render spectral centroid. **Assert 1200 Hz ≤ centroid ≤ 2500 Hz** (current Perc pad ≈ 687 Hz). Band-energy fractions with edges {100-500, 500-2000, 2000-8000}: **assert 100-500 Hz fraction < 0.45** and **500-2000 Hz fraction > 0.35**.

4. **`CLAP diffuse tail, no ring` (velocity 0.7):** 25 ms-hop RMS envelope; fit an exponential to the post-45 ms tail; extrapolate the -60 dB point. **Assert T60 ∈ [120 ms, 400 ms].** Assert the tail is monotone-decreasing after 60 ms (every hop after 60 ms ≤ 1.3× the previous smoothed hop) — no secondary attack / undamped plateau.

5. **`CLAP velocity response`:** Render velocities {0.25, 0.5, 0.75, 1.0}. **Assert peak dBFS strictly increasing.** **Assert spectral centroid non-decreasing** (soft ≤ hard). Assert the burst COUNT from test 1's method is identical (3 or 4) at velocity 0.25 and 1.0 (timing is velocity-independent).

> **Resolvability caveat (see Section 6 major):** tests 1 and 5 are the highest-risk assertions because the burst train reaches the output ONLY through the modal body plus a smooth always-on noise layer, which can fill the inter-burst valleys above the `0.25 * globalPeak` threshold. If test 1 fails, tune per Section 6 (lower `noiseLayerGain`, raise `bodyDampingB1/B3`, lower `decay`) — do NOT relax the threshold.

Also add a focused unit test **`f:\projects\iterum\plugins\membrum\tests\unit\exciters\test_clap_exciter.cpp`** (model on `plugins\membrum\tests\unit\exciters\test_noise_burst_exciter.cpp`):
- `prepare` + `trigger(1.0f)`, then `process(0)` for 2400 samples @ 48k: assert the rectified output has ≥ 3 distinct envelope humps in the first 45 ms and returns exactly 0.0 once past `totalSamples_` (`isActive()` becomes false).
- Bit-identity: two freshly-`prepare`d exciters with the same `voiceId`, both `trigger(0.8f)`, produce sample-identical output (deterministic re-seed) — FR-124.
- Allocation-free: wrap a `trigger`+`process` loop in `TestHelpers::AllocationScope` (the project's guard used in `test_exciter_bank.cpp:108`) and assert `getAllocationCount() == 0`.

**Register both new files** in `f:\projects\iterum\plugins\membrum\tests\CMakeLists.txt`: locate the existing entries for `unit/dsp/test_crash_acceptance.cpp` and `unit/exciters/test_noise_burst_exciter.cpp` and add the two new files next to them.

---

## 6. Verification loop (krate-render.exe) + tuning

Binary: `f:\projects\iterum\build\windows-x64-release\bin\Release\krate-render.exe`. It renders the compiled-in DEFAULT kit, so it reflects the Step-4 template after a rebuild. Run from the repo root:
```
build/windows-x64-release/bin/Release/krate-render.exe --note 39 --velocity 0.9 --seconds 0.6 --out f:/tmp/clap_v09.wav
build/windows-x64-release/bin/Release/krate-render.exe --note 39 --velocity 0.5 --seconds 0.6 --out f:/tmp/clap_v05.wav
```
The tool prints a JSON feature summary (peak/RMS dBFS, spectralCentroid, per-band energy fractions).

**⚠ PROTOTYPE-FIRST (burst resolvability major):** Because the exciter reaches the bus only through the ringing modal body PLUS a loud smooth always-on NoiseLayer, the inter-burst valleys can be filled above the `0.25*peak` threshold and the modal fundamental (f0 ≈ 843 Hz at size 0.25) can ring across the ~10 ms gaps. BEFORE finalizing, render `--velocity 0.9`, load the WAV, and measure the actual first-45 ms rectified envelope (a quick Node.js script reading the WAV is fine). Expect to iterate the Step-4 constants — likely `noiseLayerGain` in the 1.0-1.5 range (NOT higher) and `bodyDampingB1` toward 0.65-0.8 — so the body ring collapses to < ~15 ms between bursts and the valleys drop below 0.25×peak. Tune test-1 resolvability and test-4 tail audibility TOGETHER (they trade off against `noiseLayerGain`), not independently.

**Done criteria — ALL must hold @ vel 0.9 before declaring complete:**

| Feature (from JSON) | Required range @ vel 0.9 | Was (Perc) |
|---|---|---|
| `spectralCentroid` | 1200–2500 Hz | 687 Hz |
| band `100-500` fraction | < 0.45 | 0.9942 |
| band `500-2k` fraction | > 0.35 | 0.0047 |
| band `2k-8k` fraction | > 0.05 | 0.0011 |

Velocity check (compare the two renders): `peak(v0.9) > peak(v0.5)` and `spectralCentroid(v0.9) ≥ spectralCentroid(v0.5)`. Also confirm peak dBFS looks sane (probe-window sanity, Section 3).

If a target is missed, tune ONLY the Step-4 template constants (in this PRIORITY order) and re-render — do NOT change the exciter algorithm and do NOT relax test thresholds:
- **Residual pitched partial (fails test 2 / flatness low):** raise `modeScatter` (0.35→0.45) FIRST, then raise `modeStretch` (0.60→0.75). These are the primary anti-pitch levers. If pitch still survives, raise `bodyDampingB1`/`bodyDampingB3` to kill the modal ring faster. **Do NOT raise `material` to "brighten" — it raises the NoiseBody noise-bandpass center and lengthens the modal decay, making a pitched ring MORE likely, and NoiseBody's `modalMix` is hardwired at 0.6 (`noise_body_mapper.h:178`) so the modal layer cannot be dialed down at the template level.**
- **Centroid too LOW / 100-500 too high:** raise `noiseLayerCutoff` (0.70→0.80; the parallel layer is LOWPASS, so a higher cutoff keeps HF/centroid UP). The exciter bandpass center is already 1200-2200 Hz.
- **Bursts smearing (fails test 1):** lower `decay` (0.10→0.07), raise `bodyDampingB1` (0.65→0.80), and/or lower `noiseLayerGain` (down toward 1.0).
- **Tail too short/long (test 4):** adjust `noiseLayerDecay` (0.50 ≈ 200 ms; 0.45 ≈ 150 ms; 0.58 ≈ 300 ms).
- **Tail inaudible:** raise `noiseLayerGain` — but ONLY after re-checking test 1 still passes (they trade off).

The Catch2 acceptance tests (Section 5) are the authoritative gate for burst count, flatness, and T60 (the CLI JSON does not report those); the CLI is the fast gross-spectrum gate.

---

## 7. Constraints checklist (must NOT be violated)

- [ ] **Fast/Slow equivalence:** no Clap-specific branch added to only one of `processBlockFast`/`processBlockSlow`. Exciter called per-sample identically in both.
- [ ] **No new kit categories:** `DrumTemplate::Clap` is an internal default-kit ARCHETYPE, NOT a preset kit category. The four FIXED categories (Acoustic, Electronic, Percussive, Unnatural) are untouched.
- [ ] **maxPolyphony stays in [4,16]:** unchanged by this work.
- [ ] **modeInject rule:** Clap template leaves `modeInjectAmount = 0` (default). Never set > 0 (rings undamped forever).
- [ ] **One-shot exciter:** use the new `ClapExciter` (finishes on its own). Do NOT use `Friction` (sustains without note-off).
- [ ] **Preset compatibility:** `Clap` is appended at enum index 6 (kCount 6→7); indices 0-5 unchanged. Safety comes from `state_codec.cpp:445-454` storing the exciter as a raw int32 — NOT from any normalized-formula nesting property. Do not reorder the enum.
- [ ] **Bit-identity:** `ClapExciter::trigger()` re-seeds+resets (deterministic) so choke/steal reuse renders identically (FR-124).
- [ ] **Count/OOB tests fixed:** `test_exciter_body_matrix.cpp:479-480` → 168 (non-perf, MUST fix); `test_benchmark_144.cpp:292` array resized (OOB, MUST fix); `test_velocity_mapping.cpp` left at 6 (Clap exempt, MUST NOT add).
- [ ] **Zero compiler warnings:** float literals carry `f` suffix; explicit casts for `size_t`/`int` (MSVC C4244/C4267); `[[maybe_unused]]` on unused params; ExciterBank switch stays exhaustive.
- [ ] **Build-before-test:** never run tests before a clean Release build of `membrum_tests`.
- [ ] **`static_assert`** in `exciter_type.h` updated to `== 7`.

---

## Canonical workflow (run in this order)

1. **Write tests first** (Section 5), build, run, confirm they FAIL against the current Perc pad. Then apply Steps 1-6.
2. **Configure/build (full-path CMake required on Windows):**
   ```
   "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target membrum_tests
   ```
   Also build the render tool if it did not rebuild:
   ```
   "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release
   ```
3. **Fix ALL compiler warnings** before running any test.
4. **Run the Membrum test suite** (one run, read the summary line):
   ```
   build/windows-x64-release/bin/Release/membrum_tests.exe 2>&1 | tail -8
   ```
   All tests must pass, including the two new files and the auto-expanded matrix/allocation tests now covering `Clap × body`.
5. **Run the perf test explicitly** (it is `[.perf]`, skipped by default; the OOB fix must be exercised):
   ```
   build/windows-x64-release/bin/Release/membrum_tests.exe "[.perf]" 2>&1 | tail -8
   ```
6. **Run the render verification loop** (Section 6); confirm every JSON target lands in range and peak dBFS is sane.
7. **pluginval** (plugin source changed):
   ```
   tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3"
   ```
8. **clang-tidy:**
   ```
   ./tools/run-clang-tidy.ps1 -Target membrum -BuildDir build/windows-ninja
   ```
   Fix all warnings it reports for the changed files.
9. **Commit** (only when the user asks; branch first if on `main`).

---

## Open Questions
- Whether ClapExciter's 4-burst train (span ~42 ms) reliably keeps its excited-strike peak inside the fixed 30 ms strike-normalization probe window (drum_voice.h:1316-1318). Expected benign (burst 1 is loudest) but must be confirmed empirically in the render loop; if the peak lands in a later burst, the probe window or train length needs adjustment (out of current scope).
- Whether the burst structure survives to the output through the modal body + always-on smooth NoiseLayer well enough for acceptance test 1 (valleys < 0.25*peak) at the chosen noiseLayerGain/damping. This is the single highest-risk item and can only be resolved by the prototype-first krate-render measurement, which may force further template tuning than the starting constants provide.
- ~~modeStretch field name~~ RESOLVED (verified 2026-07-16): the field is literally `cfg.modeStretch` — see default_kit.h:229 (Cymbal template, `cfg.modeStretch = 0.6f`). Use `cfg.modeStretch` in the Clap case.