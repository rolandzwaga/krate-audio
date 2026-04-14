# State Version 6 Migration Contract -- Membrum Phase 6

**Spec**: `specs/141-membrum-phase6-ui/spec.md` (FR-080..FR-084, SC-006, SC-007)
**Plan**: `specs/141-membrum-phase6-ui/plan.md`
**Data model**: `specs/141-membrum-phase6-ui/data-model.md` section 9

This contract specifies the exact binary layout and migration rules for state v6.

---

## Binary Layout

### v5 Layout (unchanged, for reference)

```
Offset  Bytes  Field
------  -----  -----
0       4      int32   version = 5
4       4      int32   maxPolyphony
8       4      int32   voiceStealingPolicy
12      ...    (32 x PadConfigBlob)   -- 34 float64 sound params + 2 uint8 choke/bus each
...     4      int32   selectedPadIndex
...     8      float64 globalCoupling
...     8      float64 snareBuzz
...     8      float64 tomResonance
...     8      float64 couplingDelayMs
...     256    32 x float64 per-pad couplingAmount
...     2      uint16 overrideCount
...     N*6    N x { uint8 src, uint8 dst, float32 coeff }
```

### v6 Additions (appended after v5 payload)

```
Offset  Bytes  Field
------  -----  -----
+0      1280   160 x float64 per-pad macro values, pad-major:
                 pad0.tightness, pad0.brightness, pad0.bodySize, pad0.punch, pad0.complexity,
                 pad1.tightness, pad1.brightness, ..., pad31.complexity
```

**Total v6 blob size** = v5 size + 1280 bytes.

### Session-scoped parameters (NOT in IBStream)

- `kUiModeId` (280): NEVER written to IBStream; always reset to Acoustic on setState.
- `kEditorSizeId` (281): NEVER written to IBStream; always reset to Default on setState.

---

## Migration Rules

### getState (Processor -> IBStream)

```pseudo
write int32 version = 6
write v5 payload (unchanged)
for each pad in 0..31:
    write float64 padConfig[pad].macroTightness
    write float64 padConfig[pad].macroBrightness
    write float64 padConfig[pad].macroBodySize
    write float64 padConfig[pad].macroPunch
    write float64 padConfig[pad].macroComplexity
```

### setState (IBStream -> Processor)

```pseudo
read int32 version
if version > 6: return kResultFalse  (reject future versions)

// NOTE: setParamNormalized is a Controller method. These resets occur in
// Controller::setComponentState() (which receives the processor state blob),
// NOT in Processor::setState(). The Processor::setState only reads/writes the
// IBStream and MUST NOT call setParamNormalized.
//
// Session-scoped params reset FIRST (before any other handling), in Controller:
//   setParamNormalized(kUiModeId,     0.0f)   // Acoustic
//   setParamNormalized(kEditorSizeId, 0.0f)   // Default

switch version:
  case 1, 2, 3:
      run v1 -> v2 -> v3 -> v4 migration (existing code)
      // fall through to v4 defaults
      apply v4 defaults (selectedPadIndex=0, etc.)
      // fall through to v5 defaults
      apply v5 defaults (globalCoupling=0, snareBuzz=0, tomResonance=0,
                         couplingDelayMs=1.0, all couplingAmount=0.5, no overrides)
      apply v6 defaults (all 160 macros = 0.5)

  case 4:
      read v4 body (existing code)
      apply v5 defaults
      apply v6 defaults (all 160 macros = 0.5)

  case 5:
      read v4 body + v5 additions (existing code)
      apply v6 defaults (all 160 macros = 0.5)

  case 6:
      read v4 body + v5 additions (existing code)
      read 160 float64 macro values
      apply MacroMapper::reapplyAll() to recompute underlying params from macros

return kResultOk
```

### Post-setState invariant

- For `version < 6`: every macro value is exactly 0.5f (neutral).
- For `version == 6`: macro values are whatever the blob contained, clamped to [0, 1].
- For any version: `kUiModeId` == Acoustic, `kEditorSizeId` == Default.
- Audio output for v5 state loaded in a Phase 6 plugin matches Phase 5 reference audio within -120 dBFS (SC-006).

---

## Round-Trip Guarantee (FR-084)

```pseudo
original_blob = processor.getState()
processor.setState(original_blob)
roundtrip_blob = processor.getState()
assert byte_equal(original_blob, roundtrip_blob)  -- within float tolerance on floats
```

This MUST hold for every valid v6 blob, including:
- Blobs with all macros at 0.5 (neutral)
- Blobs with macros at extremes (0.0 / 1.0)
- Blobs with varying override counts (0 to 1024)
- Blobs with mixed exciter/body types across pads

---

## Kit Preset JSON Migration

**Phase 4 kit preset files** (format_version 4) accepted unchanged.
**Phase 5 kit preset files** (format_version 5) accepted unchanged; `macros` block missing -> defaults 0.5.
**Phase 6 kit preset files** (format_version 6) write:

```json
{
  "format_version": 6,
  "name": "...",
  "uiMode": "Acoustic" | "Extended",   // OPTIONAL
  "pads": [
    {
      "padIndex": N,
      /* ... Phase 4/5 fields ... */,
      "macros": {                        // OPTIONAL (defaults to 0.5 each)
        "tightness":  0.0-1.0,
        "brightness": 0.0-1.0,
        "bodySize":   0.0-1.0,
        "punch":      0.0-1.0,
        "complexity": 0.0-1.0
      }
    }
  ],
  "couplingOverrides": [ /* ... */ ]
}
```

**Loading rules**:
- Absent `"uiMode"` -> leave current `kUiModeId` unchanged (session default or previous value).
- Absent `"macros"` for a pad -> set all five macros to 0.5 for that pad.
- After loading, `MacroMapper::reapplyAll()` MUST be invoked so underlying parameters reflect loaded macros.

---

## Per-pad Preset JSON Migration

Per-pad preset JSON gains the same optional `"macros"` block. When loading a per-pad preset for pad P:
- Replace P's sound parameters (exciter/body/material/size/decay/etc.).
- Replace P's macro values (or reset to 0.5 if absent).
- **PRESERVE** P's `outputBus`, `chokeGroup`, `couplingAmount`.
- Invoke `MacroMapper::apply(P, padConfig[P])` to recompute P's underlying params.

---

## Compliance Checks

- `test_state_v6_migration.cpp::RoundTripV6` -- assert byte-equal round-trip
- `test_state_v6_migration.cpp::LoadV5IntoV6` -- assert macros=0.5, audio matches (SC-006)
- `test_state_v6_migration.cpp::LoadV1IntoV6` -- assert full migration chain
- `test_state_v6_migration.cpp::RejectV7` -- assert `version == 7` returns `kResultFalse`
- `test_ui_mode_session_scope.cpp::KitPresetOverride` -- assert JSON `"uiMode"` applies
- `test_ui_mode_session_scope.cpp::IBStreamNeverPersists` -- save/load IBStream never moves kUiModeId off Acoustic
- `test_editor_size_session_scope.cpp::ResetsToDefault` -- editor size always resets on setState
