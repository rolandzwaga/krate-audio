# Phase 0: Research - Oscillator Type-Specific Parameters

**Feature**: 068-osc-type-params
**Date**: 2026-02-19

## R-001: OscParam Enum Design

**Decision**: Use a single `enum class OscParam : uint16_t` with grouped ranges per oscillator type (gaps of 10 between groups for extensibility).

**Rationale**: A single flat enum avoids nested type hierarchies while providing compile-time safety. The gaps between groups (0, 10, 20, ...) allow future parameters to be added per oscillator type without renumbering. The `uint16_t` base type supports up to 65535 values, far more than needed. Float values are passed alongside to avoid needing multiple virtual methods.

**Alternatives Considered**:
- Per-type enums with separate virtual methods -- rejected because it would require 10+ virtual methods on OscillatorSlot, making the interface bloated.
- Template parameter packs -- rejected because virtual interfaces cannot use templates.
- String-keyed parameter maps -- rejected because string comparison is not real-time safe.

## R-002: Virtual Method vs Direct Access Pattern

**Decision**: Add a single `virtual void setParam(OscParam param, float value) noexcept` to `OscillatorSlot`, with the base class providing a silent no-op default.

**Rationale**: The existing `getOscillator()` accessor on `OscillatorAdapter` provides direct type-erased access, but callers would need `dynamic_cast` to use type-specific methods. `dynamic_cast` is forbidden in the audio path (uses RTTI/exceptions). A single virtual method with enum dispatch keeps the interface clean and real-time safe. The virtual dispatch overhead is negligible (~5ns per call at block rate, ~86 calls/sec per oscillator at 44.1kHz/512 sample blocks).

**Alternatives Considered**:
- Expose typed accessors via `OscillatorSlot` -- rejected as it would require type-unsafe casting or runtime type checks.
- Use `std::variant`-based approach -- rejected because it doesn't work with virtual interfaces and pre-allocated slots.

## R-003: if constexpr Dispatch in OscillatorAdapter

**Decision**: Implement `setParam()` in `OscillatorAdapter<OscT>` using `if constexpr` to dispatch to the correct setter method on the underlying oscillator, matching the existing pattern in `setFrequency()`, `prepare()`, and `processBlock()`.

**Rationale**: `if constexpr` generates zero-cost compile-time dispatch. Only the branch for the matching oscillator type compiles into the final binary. Unrecognized `OscParam` values fall through to no-op naturally (no default case needed). This is identical to the pattern already used throughout `oscillator_adapters.h`.

**Alternatives Considered**:
- Runtime switch inside each adapter -- unnecessary overhead since the oscillator type is known at compile time.
- CRTP (Curiously Recurring Template Pattern) -- overengineered for this use case.

## R-004: Normalized-to-DSP Value Conversion Location

**Decision**: Denormalization from VST normalized [0,1] to DSP-domain values happens in `handleOscAParamChange()` / `handleOscBParamChange()`, storing DSP-domain values in the atomic params struct. The engine/voice/adapter chain receives pre-denormalized values.

**Rationale**: This matches the existing pattern used for all other parameters (filter cutoff, distortion drive, LFO rate, etc.). The processor converts once on receipt; the engine forwards raw DSP values. Keeping denormalization in one place prevents errors from double-conversion or inconsistent mapping.

**Alternatives Considered**:
- Store normalized values and convert in the adapter -- rejected because it would create a second conversion location and complicate the adapter.
- Convert in the engine -- rejected because the engine shouldn't know about VST normalization.

## R-005: Integer Parameter Rounding

**Decision**: Integer-valued parameters (Additive Num Partials, Chaos Output Axis, and all enum dropdowns) use nearest-integer rounding: `static_cast<int>(value * (max - min) + 0.5) + min`. Particle Density is treated as a continuous float parameter (`1.0 + value * 63.0`) because `setDensity(float)` accepts fractional values and fractional particle counts are meaningful.

**Rationale**: The spec requires this (FR-008). Nearest-integer rounding ensures every integer in the range [min, max] is reachable with equal-width bands. Floor/truncation would make the maximum value unreachable for any normalized value below exactly 1.0. Particle Density was initially listed as integer-rounded but was clarified to be continuous because the underlying DSP method accepts float.

**Alternatives Considered**:
- StringListParameter's built-in `toPlain()` for dropdown-backed enums -- this already uses nearest-integer rounding internally, but we also need the same logic for true integer parameters (Num Partials, Output Axis).

## R-006: Enum Dropdown String Arrays

**Decision**: Add new dropdown string arrays to `dropdown_mappings.h` for all oscillator-type-specific enums: PolyBLEP Waveform (5), PD Waveform (8), Sync Waveform (5), Sync Mode (3), Chaos Attractor (5), Chaos Output (3), Particle Spawn Mode (3), Particle Envelope (6), Formant Vowel (5), Noise Color (6).

**Rationale**: The existing `dropdown_mappings.h` already contains dropdown string arrays for every other enum in the plugin (filter types, distortion types, delay types, etc.). Following this pattern keeps all dropdown definitions centralized.

**Key Finding**: The actual `NoiseColor` enum in `pattern_freeze_types.h` has 8 values (White through RadioStatic), but the spec only exposes 6 (White through Grey). The dropdown should expose only the 6 spec-defined values, with the NoiseColor cast clamped to 0-5. Velvet and RadioStatic are omitted because they have specialized behavior less suitable for a general-purpose oscillator noise source.

## R-007: Event-Driven Parameter Forwarding

**Decision**: `RuinaeVoice` receives type-specific parameters via explicit setter methods (`setOscAParam(OscParam, float)` and `setOscBParam(OscParam, float)`) that forward directly to `SelectableOscillator::setParam()`. `RuinaeEngine` gains matching `setOscAParam()` / `setOscBParam()` methods that iterate all 16 voices. The processor's `applyParamsToEngine()` calls these with the pre-denormalized atomic values.

**Rationale**: This exactly follows the existing event-driven pattern established for filter type, distortion drive, oscillator type, and all other parameters. The spec (FR-009, Clarification 1) explicitly requires event-driven setters, not per-block polling.

**Alternatives Considered**:
- Per-block atomic polling inside processBlock -- rejected by spec.
- Individual setter methods per parameter (30+ methods each on Voice, Engine) -- too many methods; the `OscParam` enum makes a single parametric method sufficient.

## R-008: State Persistence Strategy

**Decision**: Extend `saveOscAParams()` / `loadOscAParams()` (and B equivalents) to write/read all 30 new atomic float values after the existing 5 fields. Use a version byte or rely on stream position to detect old presets that lack the new data. On load failure (old preset), default all new fields.

**Rationale**: The existing save/load pattern writes fields sequentially. Adding fields at the end is backward-compatible for reading: old presets will have fewer bytes, and `readFloat()` will return false for the missing data. The `loadOscAParams()` function already returns bool on failure, allowing graceful fallback to defaults.

**Alternatives Considered**:
- Version-tagged binary sections -- overengineered for sequential atomic parameters.
- JSON/XML preset format -- not used by this plugin.

## R-009: UI Template Strategy

**Decision**: Replace the existing placeholder templates in `editor.uidesc` with fully wired templates containing `control-tag` attributes for all type-specific parameters. Each template must include the correct knobs and dropdowns for its oscillator type.

**Rationale**: The UIViewSwitchContainer infrastructure already exists (confirmed: `template-switch-control="OscAType"` at line 2178, and all 10 templates per oscillator already exist as placeholders). The templates just need their controls wired to the new parameter control-tags.

**Key Finding**: The existing templates are simple placeholders with dummy knobs (no `control-tag` bindings). They need to be replaced with:
- Proper `control-tag` attributes referencing the new parameter IDs
- COptionMenu for enum dropdowns
- ArcKnob for continuous parameters
- Correct parameter counts per type

## R-010: PW Knob Visual Disable (FR-016)

**Decision**: Implement PW disable via a sub-controller (IController pattern) that observes the Waveform parameter and sets the PW knob's alpha/enabled state when the waveform is not Pulse. This uses the `IDependent` thread-safety pattern already established for filter/distortion type-specific visibility.

**Rationale**: VSTGUI's `IDependent` pattern with deferred updates is the established thread-safe approach for parameter-dependent UI state changes (see `vst-guide/THREAD-SAFETY.md`). The sub-controller can observe `kOscAWaveformId` and update the PW knob's appearance accordingly.

**Alternatives Considered**:
- Custom CView subclass -- overengineered when IDependent + setAlphaValue works.
- VSTGUI's built-in `enabled` attribute with animation -- not directly parameter-driven; needs controller logic.

## R-011: Parameter ID to OscParam Mapping

**Decision**: Create a helper function `oscParamFromId(ParamID id) -> std::pair<OscParam, float_not_needed>` that maps VST parameter IDs to OscParam enum values. This is called in `applyParamsToEngine()` after the existing switch/case handling of basic osc params. The mapping is a simple offset calculation: `OscParam value = id - kOscABaseTypeSpecificId` where base is 110 for OSC A and 210 for OSC B.

**Rationale**: Since the OscParam enum values and parameter IDs are assigned in the same order within the spec, a direct mapping avoids a large switch statement. However, since the OscParam enum uses gaps (0, 10, 20, ...) and the parameter IDs use contiguous ranges (110, 111, ...), a lookup table or switch mapping is needed to bridge the gap.

**Decision (refined)**: Use a static lookup table `constexpr OscParam kParamIdToOscParam[]` indexed by `(paramId - 110)` for OSC A or `(paramId - 210)` for OSC B, mapping each parameter ID offset to the corresponding OscParam enum value. This is a compile-time constant array with O(1) lookup.
