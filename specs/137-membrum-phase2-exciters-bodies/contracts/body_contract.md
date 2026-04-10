# Body Backend Contract

**Applies to:** Every alternative type in `Membrum::BodyBank`'s `std::variant` (`MembraneBody`, `PlateBody`, `ShellBody`, `StringBody`, `BellBody`, `NoiseBody`).

Structural contract (no virtual base), consistent with FR-001.

## Required public interface

```cpp
struct Body {
    // Lifecycle
    void prepare(double sampleRate, uint32_t voiceId) noexcept;
    void reset(Krate::DSP::ModalResonatorBank& sharedBank) noexcept;

    // Note-on reconfigure hook.
    // Called by DrumVoice once per note, BEFORE the first process() of the note.
    // The body either (a) configures the sharedBank for modal bodies, or
    // (b) configures its own resonator (String, Noise Body noise path).
    void configureForNoteOn(
        Krate::DSP::ModalResonatorBank& sharedBank,
        const VoiceCommonParams& params,
        float pitchEnvelopeStartHz
    ) noexcept;

    // Per-sample audio process.
    // Modal bodies delegate to sharedBank.processSample(excitation).
    // String body uses its own waveguide.
    // Noise Body mixes sharedBank output + its own noise layer.
    [[nodiscard]] float processSample(
        Krate::DSP::ModalResonatorBank& sharedBank,
        float excitation
    ) noexcept;
};
```

## Shared-bank semantics (FR-026)

- The `Krate::DSP::ModalResonatorBank` is **owned by the `BodyBank`**, passed by reference to every body backend's `configureForNoteOn()` and `processSample()`.
- Modal bodies (`Membrane`, `Plate`, `Shell`, `Bell`, and the modal portion of `NoiseBody`) **MUST NOT** own their own bank.
- `String` body **MUST NOT** touch the shared bank; it uses `WaveguideString` instead. Its `processSample()` ignores the `sharedBank` reference and routes directly through `waveguide_`.
- `NoiseBody` uses the shared bank for its modal layer PLUS its own `NoiseOscillator` + `SVF` for the noise layer.

### Reconfiguration on body-model switch

When `BodyBank::configureForNoteOn()` detects `pendingType_ != currentType_`:

1. Call `reset(sharedBank)` on the **new** alternative. This clears the bank's filter state (`sharedBank.reset()`) for modal bodies.
2. Emplace the new alternative into the variant.
3. Update `currentType_ = pendingType_`.
4. Call the new alternative's `configureForNoteOn()`.

The ringing tail from the previous body continues undisturbed UNTIL `setModes()` is called (which clears state), at which point the tail is gone and the new body takes over. This is the deferred-switch contract (FR-005): the switch happens at note-on, not during a ringing tail.

### Mid-note cross-fade prohibition

- Phase 2 **MUST NOT** attempt cross-fading between body models while a voice is sounding. This is explicitly out of scope per spec clarifications.
- Changes made via `setBodyModel()` during a sounding voice are **queued**; the change applies at the next `noteOn()`.
- The implementation **MUST NOT** crash, allocate, or produce NaN/Inf if `setBodyModel()` is called rapidly during a sounding voice (acceptance scenario US3-5).

## Modal behavior (Membrane, Plate, Shell, Bell, NoiseBody modal layer)

### Frequency ratios (SC-002)

Each modal body's `configureForNoteOn()` **MUST** configure the shared bank with mode frequencies derived from the body's fundamental × the body's published ratio table:

| Body | Ratio source | Mode count | Tolerance |
|------|--------------|------------|-----------|
| Membrane | `membrane_modes.h` Bessel ratios | 16 | ±2% |
| Plate | `plate_modes.h` Kirchhoff square-plate ratios | 16 | ±3% |
| Shell | `shell_modes.h` Euler-Bernoulli free-free beam | 12 | ±3% |
| Bell | `bell_modes.h` Chladni ratios | 16 | ±3% |
| NoiseBody | `plate_modes.h` extended to 40 | 40 (start) | ±3% |

A unit test **MUST** measure the first N partial frequencies of each body's output (excited by an impulse at low Material for long decay) and assert the measured ratios match the reference table within the per-body tolerance.

### Damping (Chaigne-Lambourg)

Each body's mapping helper produces `(decayTime, brightness, stretch)` scalars that are passed to `ModalResonatorBank::setModes()`. The bank internally computes per-mode damping coefficients from these scalars plus the mode frequencies.

- `Material` → `brightness` (low material = woody, high material = metallic). Per Phase 1: `brightness = material`.
- `Decay` → `decayTime` multiplier; per-body defaults documented in the mapping helper.
- `UnnaturalZone::modeStretch` → `stretch` (passed through from DrumVoice).

### Strike position

Each modal body's mapping helper computes per-mode amplitude from the Strike Position scalar using body-specific math:

- **Membrane**: `|J_m(j_{mn} · r/a)|` where `r/a = strikePos * 0.9` (Phase 1 formula).
- **Plate**: `sin(m · π · x₀/a) · sin(n · π · y₀/b)` with `(x₀, y₀)` derived from the single Strike Position.
- **Shell**: `sin(k · π · x₀/L)` with `x₀/L = strikePos`.
- **Bell**: Chladni radial approximation `r/R = strikePos`.
- **NoiseBody** (modal layer): same as Plate.

## String body addenda

- `StringBody` owns a `Krate::DSP::WaveguideString` instance, not a modal bank.
- `configureForNoteOn()` calls `waveguide_.setFrequency(pitchEnvelopeStartHz)`, `setDecay(...)`, `setBrightness(...)`, `setPickPosition(...)`.
- `processSample(sharedBank, excitation)` **ignores** `sharedBank` and calls `waveguide_.process(excitation)`.
- Output partials **MUST** be harmonic (integer-multiple spacing within ±1% — acceptance scenario US2-4, SC-002 String row).

## Noise Body addenda

- Two-layer hybrid: modal (via sharedBank with plate ratios) + noise (via `NoiseOscillator` + `SVF` bandpass).
- `configureForNoteOn()` sets up BOTH layers: shared bank configured with `kPlateRatios[0..kModeCount)` scaled by fundamental, AND noise filter configured with time-varying cutoff.
- `processSample(sharedBank, excitation)` computes:
  ```
  modalOut = sharedBank.processSample(excitation);
  noiseOut = noiseEnvelope_.process() * noiseFilter_.process(noise_.process());
  return modalMix_ * modalOut + noiseMix_ * noiseOut;
  ```
- Default mix: `modalMix_ = 0.6, noiseMix_ = 0.4`.
- Starting mode count: **40** (FR-062). Reduce empirically if CPU budget blown. Final count recorded in `plan.md`.

## Real-time safety invariants

Same as the exciter contract:

- All methods `noexcept`.
- No heap allocation post-`prepare()`.
- No locks, exceptions, I/O, logging in the per-sample path.
- `configureForNoteOn()` may do substantial work (compute mode arrays, call `setModes()`) but **MUST NOT** allocate.
- `processSample()` MUST complete in bounded time regardless of body type. Worst case (NoiseBody, 40 modes) profiled and documented in `plan.md`.

## Finite-output guarantee

- `processSample()` **MUST** return a finite value (no NaN, no Inf, no denormal) for any excitation in `[-10.0f, +10.0f]` (generous upper bound accommodating exciter output before soft-clip).
- Denormal protection: FTZ/DAZ is enabled at the processor level (Phase 1 carryover); individual bodies rely on this plus `ModalResonatorBank`'s internal `flushSilentModes()`.

## Swap semantics

- `BodyBank::setBodyModel(type)` sets `pendingType_` only.
- The actual swap happens inside `configureForNoteOn()` called at the start of each `DrumVoice::noteOn()`:
  1. If `pendingType_ != currentType_`:
     a. `sharedBank_.reset()` (clears modal state).
     b. Emplace the new alternative.
     c. `currentType_ = pendingType_`.
  2. Call the new alternative's `configureForNoteOn(sharedBank_, params, pitchHz)`.
- Between a `setBodyModel()` call and the next `noteOn()`, the previously-active body continues producing audio (the tail of the last note). The deferred switch does not disturb the tail.

## Test coverage requirements

Every body backend MUST have unit tests covering:

1. **Allocation detector** — `configureForNoteOn()` and `processSample()` zero allocation.
2. **Modal ratios** (Membrane/Plate/Shell/Bell/NoiseBody modal) — measure first-N partial ratios, assert within tolerance.
3. **Harmonic ratios** (String) — measure integer-ratio partials within ±1%.
4. **Size sweep** — sweep Size parameter, assert fundamental frequency spans ≥ 1 octave (acceptance scenario US4-1).
5. **Decay sweep** — sweep Decay parameter, assert RT60 changes by ≥ 3× (US4-2).
6. **Finite-output sanity** — process 1 s of audio with all 6 exciters; assert no NaN/Inf.
7. **Sample rate sweep** — run at 22050/44100/48000/96000/192000 Hz; assert correct modal frequencies relative to sample rate (SC-007).
8. **Mid-note body switch deferral** — call `setBodyModel()` during a sounding note; assert no crash, no NaN, and the tail of the old body continues.
9. **Shared-bank isolation** — after a Membrane note decays, trigger a String note; assert the Membrane modal bank state does NOT affect the String waveguide output.
