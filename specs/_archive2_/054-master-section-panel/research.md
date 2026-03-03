# Research: Master Section Panel - Wire Voice & Output Controls

**Branch**: `054-master-section-panel` | **Date**: 2026-02-14

## Research Tasks

This spec is a straightforward parameter pipeline wiring task. Research was minimal since all patterns already exist in the codebase.

### R-001: Existing VoiceMode Parameter Registration

**Decision**: The `kVoiceModeId` (ID 1) is already fully registered as a `StringListParameter` with values "Poly" and "Mono" via `createDropdownParameter()` in `registerGlobalParams()`. It is already handled in `handleGlobalParamChange()`, saved/loaded in state persistence, and forwarded to `engine_.setMode()` in the processor. The only missing piece is a uidesc `control-tag` and `COptionMenu` control.

**Rationale**: No parameter registration changes needed -- only UI binding.

**Alternatives considered**: None. The parameter infrastructure is complete.

### R-002: Width Parameter Denormalization Formula

**Decision**: Linear scaling `norm * 2.0 = engine value`. Normalized 0.0-1.0 maps to 0.0-2.0 engine value. Default: 0.5 normalized = 1.0 engine value = natural stereo width.

**Rationale**: The engine's `setStereoWidth(float)` accepts 0.0-2.0 where 0.0=mono, 1.0=natural, 2.0=extra-wide. Simple linear mapping avoids complexity. The spec explicitly mandates this formula.

**Alternatives considered**:
- RangeParameter (rejected: the existing MasterGain uses the same pattern -- plain Parameter with manual denormalization in `handleGlobalParamChange()`)
- Logarithmic scaling (rejected: linear is sufficient for stereo width, and spec explicitly requires linear)

### R-003: Spread Parameter Mapping

**Decision**: 1:1 mapping. Normalized 0.0-1.0 maps directly to engine value 0.0-1.0. Default: 0.0 normalized = 0.0 engine value = all voices centered.

**Rationale**: The engine's `setStereoSpread(float)` accepts 0.0-1.0. No conversion needed.

**Alternatives considered**: None. The range matches exactly.

### R-004: Backward-Compatible State Loading Pattern

**Decision**: After reading the existing 4 fields (masterGain, voiceMode, polyphony, softLimit), attempt to read Width and Spread. If `readFloat()` returns false (EOF -- old preset format), keep default values. Do NOT return false from the load function for these optional fields.

**Rationale**: The spec explicitly requires this pattern. The existing `loadGlobalParams()` returns false on read failure for existing fields (which is correct -- a corrupt preset should fail to load). But for new fields added after the original format, EOF is expected behavior for old presets, not an error.

**Alternatives considered**:
- Version field in state (rejected: over-engineering for 2 extra floats; the read-or-default pattern is simpler and already suggested by the spec)
- Separate state section (rejected: would break the existing linear state format)

### R-005: Panel Layout Vertical Space Budget

**Decision**: Use tighter vertical spacing (Option A from plan) to fit all controls within 160px height.

**Rationale**: Adding the Voice Mode dropdown row consumes ~20px of vertical space. The original layout had enough slack (Polyphony at y=14, Output knob at y=48, Width/Spread at y=100, Soft Limit at y=142) to absorb this by compressing gaps. The final layout uses 4px minimum gaps between all elements and ends exactly at y=160.

**Layout budget**:
```
y=0:   Top border (FieldsetContainer)
y=14:  Voice Mode row (Mode label + dropdown + gear) -- 18px
y=34:  Polyphony row -- 18px
y=56:  Output knob -- 36px
y=92:  Output label -- 10px
y=104: Width/Spread knobs -- 28px
y=132: Width/Spread labels -- 10px
y=144: Soft Limit toggle -- 16px
y=160: Bottom border (panel end)
```

**Alternatives considered**:
- Remove Polyphony label (rejected: spec FR-003 requires it visible)
- Increase panel height (rejected: spec FR-021 requires 120x160)
- Smaller knobs for Width/Spread (rejected: 28x28 already small; spec FR-008/FR-014 specify 28x28)

### R-006: VoiceMode Dropdown Display Text

**Decision**: The COptionMenu will display "Poly" and "Mono" (the registered StringListParameter values). The spec mentions "Polyphonic" and "Mono" as display items. However, the parameter is already registered with `STR16("Poly")` and `STR16("Mono")`. Changing the registration would affect automation display and potentially break existing presets that reference the parameter by name.

**Rationale**: The spec says "It MUST display two items: 'Polyphonic' and 'Mono'" (FR-002). This conflicts with the existing registration which uses "Poly". Since changing the registration to "Polyphonic" is a safe rename (StringListParameter uses index-based normalization, not string matching), and the spec explicitly requires "Polyphonic" display text, the registration should be updated from "Poly" to "Polyphonic". This is a one-word change in `registerGlobalParams()` and does NOT affect state persistence (which stores the integer index, not the string).

**Final decision**: Update the StringListParameter registration from `STR16("Poly")` to `STR16("Polyphonic")` per spec FR-002.

**Alternatives considered**:
- Keep "Poly" as registered (rejected: spec FR-002 explicitly requires "Polyphonic")
- Use COptionMenu menu-names override (rejected: unnecessary complexity when the registration itself can be changed)
