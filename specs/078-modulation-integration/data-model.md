# Data Model: Arpeggiator Modulation Integration

**Feature**: 078-modulation-integration
**Date**: 2026-02-24

## Entity Overview

This feature introduces no new entities. It extends existing entities with new values.

## Entity: RuinaeModDest (Extended)

**Location**: `plugins/ruinae/src/engine/ruinae_engine.h`
**Type**: `enum class : uint32_t`
**Layer**: Plugin (engine)

### Existing Values (unchanged)

| Name | Value | Description |
|------|-------|-------------|
| GlobalFilterCutoff | 64 | Global filter cutoff offset |
| GlobalFilterResonance | 65 | Global filter resonance offset |
| MasterVolume | 66 | Master volume offset |
| EffectMix | 67 | Effect chain mix offset |
| AllVoiceFilterCutoff | 68 | Forwarded to all voices' filter cutoff |
| AllVoiceMorphPosition | 69 | Forwarded to all voices' morph position |
| AllVoiceTranceGateRate | 70 | Forwarded to all voices' trance gate rate |
| AllVoiceSpectralTilt | 71 | Forwarded to all voices' spectral tilt |
| AllVoiceResonance | 72 | Forwarded to all voices' filter resonance |
| AllVoiceFilterEnvAmt | 73 | Forwarded to all voices' filter env amount |

### New Values (this feature)

| Name | Value | Description | UI Index |
|------|-------|-------------|----------|
| ArpRate | 74 | Arp rate/speed modulation | 10 |
| ArpGateLength | 75 | Arp gate length modulation | 11 |
| ArpOctaveRange | 76 | Arp octave range modulation | 12 |
| ArpSwing | 77 | Arp swing modulation | 13 |
| ArpSpice | 78 | Arp spice amount modulation | 14 |

### Invariant

```
static_assert(ArpRate == GlobalFilterCutoff + 10)
```

This invariant protects `modDestFromIndex()` which maps UI dropdown indices to enum values via `GlobalFilterCutoff + index`.

## Entity: kGlobalDestNames (Extended)

**Location**: `plugins/shared/src/ui/mod_matrix_types.h`
**Type**: `constexpr std::array<ModDestInfo, 15>`
**Layer**: Shared plugin infrastructure

### New Entries (appended at indices 10-14)

| Index | fullName | hostName | abbreviation |
|-------|----------|----------|--------------|
| 10 | "Arp Rate" | "Arp Rate" | "ARate" |
| 11 | "Arp Gate Length" | "Arp Gate" | "AGat" |
| 12 | "Arp Octave Range" | "Arp Octave" | "AOct" |
| 13 | "Arp Swing" | "Arp Swing" | "ASwg" |
| 14 | "Arp Spice" | "Arp Spice" | "ASpc" |

## Entity: kGlobalDestParamIds (Extended)

**Location**: `plugins/ruinae/src/controller/controller.cpp`
**Type**: `constexpr std::array<Steinberg::Vst::ParamID, 15>`
**Layer**: Controller

### New Entries (appended at indices 10-14)

| Index | Param ID Constant | Numeric Value | Description |
|-------|-------------------|---------------|-------------|
| 10 | kArpFreeRateId | 3006 | Arp free rate knob (always, see limitation) |
| 11 | kArpGateLengthId | 3007 | Arp gate length knob |
| 12 | kArpOctaveRangeId | 3002 | Arp octave range selector |
| 13 | kArpSwingId | 3008 | Arp swing knob |
| 14 | kArpSpiceId | 3290 | Arp spice knob |

**Limitation**: Index 10 always maps to kArpFreeRateId regardless of tempo-sync mode. Dynamic mode-aware switching deferred to Phase 11.

## Modulation Application Formulas

### Arp Rate (FR-008)

```
Free mode:   effectiveRate = baseRate * (1.0 + 0.5 * offset)
             clamped to [0.5, 50.0] Hz

Tempo-sync:  effectiveDuration = baseDuration / (1.0 + 0.5 * offset)
             (implemented as: compute Hz = 1/duration, use as free rate)
             clamped to positive values
```

- offset range: [-1, +1]
- Modulation range: +/-50% of current rate
- When offset = 0: effectiveRate = baseRate exactly

### Arp Gate Length (FR-009)

```
effectiveGate = baseGate + 100.0 * offset
clamped to [1.0, 200.0] percent
```

- offset range: [-1, +1]
- Modulation range: +/-100 percentage points

### Arp Octave Range (FR-010)

```
effectiveOctave = baseOctave + round(3.0 * offset)
clamped to [1, 4]
```

- offset range: [-1, +1]
- Modulation range: +/-3 octaves (integer-rounded)
- setOctaveRange() only called when effective value changes (tracked by prevArpOctaveRange_)

### Arp Swing (FR-011)

```
effectiveSwing = baseSwing + 50.0 * offset
clamped to [0.0, 75.0] percent
```

- offset range: [-1, +1]
- Modulation range: +/-50 percentage points

### Arp Spice (FR-012)

```
effectiveSpice = baseSpice + offset
clamped to [0.0, 1.0]
```

- offset range: [-1, +1]
- Bipolar: negative offset reduces spice below base

## State Transitions

No new states. The arp parameters are modulated transparently on every block where the arp is enabled. When arp is disabled, mod offsets are not read (FR-015 optimization). When re-enabled, the first block reads whatever offset the ModulationEngine most recently computed.

## Validation Rules

1. All enum values must be < kMaxModDestinations (128): 74-78 < 128 -- satisfied
2. kGlobalDestNames.size() must equal kNumGlobalDestinations: enforced by static_assert
3. kGlobalDestParamIds.size() must equal kGlobalDestNames.size(): enforced by static_assert
4. ArpRate must equal GlobalFilterCutoff + 10: enforced by new static_assert (FR-020)
5. All mod application math must be real-time safe: verified by construction (no allocations, no locks)
