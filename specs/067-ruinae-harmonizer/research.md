# Research: Ruinae Harmonizer Integration

**Feature**: 067-ruinae-harmonizer
**Date**: 2026-02-19

## Research Questions and Findings

### R-001: HarmonizerEngine API and Signal Flow

**Question**: How does the existing HarmonizerEngine work and what is its exact API?

**Decision**: Use HarmonizerEngine as-is, with no modifications to the DSP library.

**Rationale**: The HarmonizerEngine at `dsp/include/krate/dsp/systems/harmonizer_engine.h` is a fully implemented, tested Layer 3 component. It takes mono input and produces stereo output with built-in dry/wet mixing. All 4 voices are always pre-allocated; only `numActiveVoices_` are processed. The engine supports Chromatic and Scalic harmony modes, 4 pitch shift algorithms, formant preservation, and shared-analysis FFT optimization. It was implemented and tested in specs 064 and 065.

**Key API details**:
- `prepare(double sampleRate, std::size_t maxBlockSize)` -- pre-allocates all buffers
- `process(const float* input, float* outputL, float* outputR, std::size_t numSamples)` -- mono in, stereo out
- `getLatencySamples()` -- returns latency for current pitch shift mode (not worst-case)
- Global setters: `setHarmonyMode()`, `setNumVoices()`, `setKey()`, `setScale()`, `setPitchShiftMode()`, `setFormantPreserve()`, `setDryLevel()`, `setWetLevel()`
- Per-voice setters: `setVoiceInterval()`, `setVoiceLevel()`, `setVoicePan()`, `setVoiceDelay()`, `setVoiceDetune()`

**Alternatives considered**: None. The engine is purpose-built for this exact integration.

---

### R-002: Effects Chain Architecture and Signal Path

**Question**: How does the Ruinae effects chain currently work, and where does the harmonizer slot fit?

**Decision**: Insert harmonizer between delay output and reverb input in `processChunk()`.

**Rationale**: The current signal flow in `RuinaeEffectsChain::processChunk()` is:
1. Phaser (stereo in-place)
2. Delay (5 types with crossfade, latency compensation)
3. Reverb (stereo in-place)

The harmonizer slot goes between steps 2 and 3. The delay output (stereo) is summed to mono for harmonizer input, then the harmonizer's stereo output replaces the signal going into reverb.

The effects chain already has a pattern for enable/disable flags (`delayEnabled_`, `reverbEnabled_`, `phaserEnabled_`) with setter methods. The harmonizer follows the same pattern.

**Alternatives considered**:
- Placing harmonizer before delay: Rejected per spec (must be after delay, before reverb)
- Running harmonizer in stereo: Rejected because HarmonizerEngine takes mono input; this is by design for pitch tracking consistency

---

### R-003: Latency Reporting Strategy

**Question**: How should the harmonizer's latency be incorporated into the effects chain's total latency?

**Decision**: Add harmonizer PhaseVocoder worst-case latency to existing spectral delay latency during `prepare()`.

**Rationale**: The existing `targetLatencySamples_` in RuinaeEffectsChain is set to the spectral delay's latency (FFT size 4096 samples at default). Per FR-019, we must add the harmonizer's worst-case (PhaseVocoder) latency of 5120 samples (FFT 4096 + Hop 1024). The combined constant is 9216 samples at default settings.

The `getLatencySamples()` method on HarmonizerEngine returns latency for the *current* mode, so during `prepare()` we must temporarily set PhaseVocoder mode, query the latency, then restore the default mode.

The total latency is held constant regardless of runtime pitch shift mode changes (FR-020), which matches the existing behavior for spectral delay type switching.

**Key insight**: The Ruinae processor does NOT currently call `setInitialDelay()` to report latency to the host. The latency value is only used internally for delay-type crossfade compensation. This behavior is not changed by this spec.

**Alternatives considered**:
- Dynamic latency reporting (change when pitch mode changes): Rejected per FR-020; constant worst-case is simpler and avoids host PDC recalculation artifacts
- Not adding harmonizer latency: Rejected because it would cause timing misalignment when switching between delay types while harmonizer is in PhaseVocoder mode

---

### R-004: Parameter Registration Pattern

**Question**: What pattern should the harmonizer parameters follow?

**Decision**: Follow the exact pattern established by `phaser_params.h`.

**Rationale**: The Ruinae plugin uses a consistent pattern across all parameter groups:
1. Struct with `std::atomic<>` fields (denormalized values)
2. `handle{Effect}ParamChange()` -- denormalization switch statement
3. `register{Effect}Params()` -- controller registration
4. `format{Effect}Param()` -- display formatting
5. `save{Effect}Params()` / `load{Effect}Params()` -- binary serialization
6. `load{Effect}ParamsToController()` -- controller state sync

Each function is `inline` in a single header file. Parameter changes flow: Host -> Processor::processParameterChanges() -> handleXxxParamChange() -> atomic store -> Processor::applyParamsToEngine() -> Engine setter -> EffectsChain setter -> DSP engine.

The phaser has 10 params (rate, depth, feedback, mix, stages, center freq, spread, waveform, sync, note value). The harmonizer has 28 params (8 global + 4x5 per-voice), which is more complex but uses the same structural pattern.

**Alternatives considered**:
- Separate header per function (register, handle, save): Rejected because all existing params use single-header pattern
- Shared harmonizer params template: Premature abstraction; only one plugin uses this now

---

### R-005: State Serialization Version Strategy

**Question**: How should harmonizer state be added to the serialization format?

**Decision**: Increment state version from 15 to 16, append harmonizer data after existing v15 data.

**Rationale**: The binary state format uses a version number at the start. Each version adds data at the end. The v16 data consists of:
1. All harmonizer param values (from `saveHarmonizerParams()`)
2. Harmonizer enabled flag (int8)

Old presets (v1-v15) will load with harmonizer defaults:
- Disabled (kHarmonizerEnabledId default = 0.0)
- 0 active voices
- Chromatic mode
- 0 dB dry, -6 dB wet

This ensures backward compatibility -- existing presets sound identical after the update.

**Alternatives considered**: None. This is the established pattern, there is no alternative.

---

### R-006: UI Panel Architecture

**Question**: How should the harmonizer UI panel be structured?

**Decision**: Follow the existing FX panel pattern (header + collapsible detail), add voice row dimming via alpha.

**Rationale**: The existing FX panels (Delay, Reverb, Phaser) all use the same pattern:
1. Header row with Enable toggle + label + expand/collapse chevron
2. Detail panel (CViewContainer) initially hidden, toggled by chevron
3. `toggleFxDetail(index)` manages mutual exclusion (opening one collapses others)

The harmonizer extends this to 4 panels. Voice row dimming uses `setAlphaValue()` on CViewContainers, which visually greys out controls without removing them from the layout. This matches FR-022 (all 4 rows always visible, inactive dimmed).

The controller tracks voice row containers via `custom-view-name` attributes in editor.uidesc, captured in `verifyView()`.

**Alternatives considered**:
- Dynamic show/hide of voice rows: Rejected per FR-022 spec (all rows always visible)
- Separate sub-controller for harmonizer: Overkill for parameter forwarding; existing pattern uses main Controller

---

### R-007: Stereo-to-Mono Input Handling

**Question**: How should the stereo delay output be converted to mono for the harmonizer?

**Decision**: Simple (L+R) * 0.5 averaging into a pre-allocated scratch buffer.

**Rationale**: The HarmonizerEngine takes mono input. The delay output is stereo. Per FR-021, we sum left and right channels with equal weight: `mono[i] = (left[i] + right[i]) * 0.5f`. This is done into `harmonizerMonoScratch_`, a pre-allocated buffer in the effects chain.

The 0.5 factor prevents clipping when L and R are correlated (which they often are for mono synth sources panned center). The harmonizer's internal dry/wet mixing then handles the blend.

**Alternatives considered**:
- Left channel only: Would lose right channel information from stereo delay types (PingPong, panned tape heads)
- Power-preserving sum (L+R)/sqrt(2): Unnecessary complexity; the harmonizer's own level controls handle gain staging

---

### R-008: Per-Voice Parameter ID Layout

**Question**: How should per-voice parameter IDs be organized within the 2800-2899 range?

**Decision**: Global params at 2800-2807, per-voice in 10-ID blocks starting at 2810.

**Rationale**: Layout:
- 2800-2807: 8 global params
- 2808-2809: Reserved
- 2810-2814: Voice 1 (5 params)
- 2815-2819: Reserved
- 2820-2824: Voice 2
- 2825-2829: Reserved
- 2830-2834: Voice 3
- 2835-2839: Reserved
- 2840-2844: Voice 4
- 2845-2899: Reserved for future expansion

The 10-ID-per-voice pattern allows easy calculation: `voiceIndex = (paramId - 2810) / 10`, `paramOffset = (paramId - 2810) % 10`. This is cleaner than contiguous allocation and leaves room for adding per-voice parameters later.

**Alternatives considered**:
- Contiguous allocation (V1 at 2808-2812, V2 at 2813-2817): Harder to calculate voice index, no expansion room
- Formula-based IDs: More error-prone than explicit enum values

---

### R-009: Voice Row Dimming Implementation

**Question**: How should inactive voice rows be visually dimmed in the UI?

**Decision**: Use `CViewContainer::setAlphaValue()` to dim/undim voice row containers.

**Rationale**: VSTGUI's `CView::setAlphaValue()` (inherited by all views) sets the alpha transparency of a view and its children. Setting alpha to 0.3 makes controls appear "greyed out" while keeping them visible and in the layout. This is the simplest cross-platform approach.

The dimming is triggered in `Controller::setParamNormalized()` when `kHarmonizerNumVoicesId` changes. The controller checks the new value and updates all 4 voice row containers.

**Alternatives considered**:
- Disable/enable individual controls: More complex, would need to track each control separately
- CSS-like styling: VSTGUI doesn't have CSS; closest is bitmap overlays which are heavier
- Custom overlay view: Unnecessary complexity for a simple alpha dim effect
