# Research: Ruinae Flanger Effect

**Spec**: [spec.md](spec.md) | **Date**: 2026-03-12

## Research Questions & Findings

### R-001: Flanger DSP Algorithm Design

**Question**: What is the optimal flanger algorithm topology for this context (real-time VST3, stereo, feedback, LFO modulation)?

**Decision**: Classic single-delay-line flanger with feedback, using the same structural pattern as the existing Phaser class.

**Rationale**: The standard flanger topology is well-understood, computationally cheap, and maps directly onto existing KrateDSP primitives. The Phaser class provides a proven structural template for prepare/reset/processStereo lifecycle, LFO management, parameter smoothing, and stereo spread -- only the core processing differs (delay line vs allpass cascade).

**Alternatives Considered**:
- **Multi-tap flanger**: Multiple delay taps at different modulation depths. Rejected: adds complexity without meeting the spec requirements. Can be added as a parameter later.
- **Allpass-based flanger**: Using allpass filters instead of a delay line. Rejected: that is literally a phaser, which already exists.
- **Through-zero flanger**: Requires negative delay (advance the signal). Rejected: significantly more complex, requires lookahead buffer, and is not requested in the spec.

### R-002: Delay Line Sizing

**Question**: How should the delay line be sized for the 0.3-4.0ms sweep range?

**Decision**: Allocate delay line with `maxDelaySeconds = 0.010f` (10ms), providing comfortable headroom above the 4.0ms maximum sweep.

**Rationale**: The spec states a max sweep of 4.0ms. Using 10ms provides headroom for safety margins, potential future range extension, and avoids any edge-case issues with interpolation reading near the buffer boundary. At 192 kHz (maximum expected sample rate), 10ms = 1920 samples. `DelayLine::prepare()` rounds up to next power of 2 (2048 samples), which is a trivial allocation.

**Alternatives Considered**:
- **5ms buffer**: Minimal headroom. Rejected: too tight; interpolation near boundary could cause issues.
- **20ms buffer**: Excessive for flanging. Rejected: unnecessarily large, though memory cost is trivial.

### R-003: LFO-to-Delay Mapping

**Question**: How should the LFO output (bipolar -1 to +1) map to delay time?

**Decision**: Map LFO output to delay time using:
```
unipolar = lfoValue * 0.5f + 0.5f;  // [-1,+1] -> [0,1]
delayMs = minDelayMs + unipolar * (maxDelayMs - minDelayMs);
```
Where `minDelayMs = 0.3f` (constant) and `maxDelayMs` is depth-dependent:
```
maxDelayMs = kMinDelayMs + depth * (kMaxDelayMs - kMinDelayMs);
// At depth=0.0: maxDelayMs = 0.3ms (no sweep)
// At depth=1.0: maxDelayMs = 4.0ms (full sweep)
```

**Rationale**: Per the spec clarification, Depth=0.0 maps to 0.3ms and Depth=1.0 maps to 4.0ms. The LFO sweeps between `kMinDelayMs` (0.3ms, the floor) and the depth-controlled ceiling. When depth=0, the ceiling equals the floor, so there is no sweep -- just a static 0.3ms delay. This matches the spec exactly.

### R-004: Feedback Safety

**Question**: How should feedback be kept stable at extreme values?

**Decision**: Apply `std::tanh()` soft-clipping to the feedback signal before summing with input, and clamp the feedback coefficient to +/-0.98 internally.

**Rationale**: This matches the Phaser's proven feedback safety approach. `tanh()` prevents runaway oscillation by saturating the feedback signal. The 0.98 clamp provides an additional safety margin. `detail::flushDenormal()` handles denormal accumulation in the feedback path.

**Alternatives Considered**:
- **Hard clipping**: `std::clamp(-1, +1)` on feedback signal. Rejected: produces harsh artifacts.
- **No clamping, rely on tanh alone**: Rejected: tanh alone allows very high energy at |feedback| = 1.0; the 0.98 clamp provides a practical safety margin.

### R-005: Modulation Slot Architecture

**Question**: How should the modulation type selector be implemented in RuinaeEffectsChain?

**Decision**: Replace `phaserEnabled_` with a `modulationType_` enum field (`ModulationType { None, Phaser, Flanger }`). Implement a crossfade mechanism identical to the delay type crossfade pattern already in the effects chain (linear ramp, 30ms).

**Rationale**: The effects chain already has a proven crossfade pattern for delay type switching with `crossfading_`, `crossfadeAlpha_`, `crossfadeIncrement_`, `startCrossfade()`, and `completeCrossfade()`. The modulation slot needs an identical mechanism but with its own state variables (to allow independent crossfading of modulation effects and delay types simultaneously).

**Implementation detail**: Add `modCrossfading_`, `modCrossfadeAlpha_`, `modCrossfadeIncrement_`, `activeModType_`, `incomingModType_` fields. During crossfade, both phaser and flanger process the same input; outputs are linearly blended. After crossfade completes, only the active effect processes audio.

### R-006: Preset State Migration

**Question**: How should old presets (with `phaserEnabled_` boolean) be migrated to the new modulation type selector?

**Decision**: The state load path reads the legacy `phaserEnabled_` boolean from the stream (maintaining stream order compatibility) and maps it:
- `phaserEnabled_ = true` -> `modulationType = Phaser`
- `phaserEnabled_ = false` -> `modulationType = None`

New presets write `modulationType` as an int32 in place of the boolean, plus flanger parameters afterward. A version marker or stream position detection differentiates old vs new format.

**Rationale**: The state stream is ordered and sequential (IBStreamer). The phaser enable boolean is read at a fixed position in the stream. For new saves, we write the modulation type integer in the same position. For old loads, the int8 boolean (0 or 1) is safely interpretable: 0 = None, 1 = Phaser (matching the old semantics). To distinguish old from new format, we use the existing state version field or attempt to read flanger params after -- if they fail to parse, we know it is an old preset.

**Alternatives Considered**:
- **Separate migration function**: Too complex; the stream position approach is simpler.
- **Breaking old presets**: Rejected: constitution and spec require backward compatibility.

### R-007: Parameter ID Allocation

**Question**: Which parameter IDs should be used for flanger parameters?

**Decision**: Use the 1910-1919 range as specified in FR-015. Confirmed this range is completely unoccupied in `plugin_ids.h`.

**ID Allocation**:
| ID | Parameter | Type |
|----|-----------|------|
| 1910 | `kFlangerRateId` | Rate (0.05-5.0 Hz) |
| 1911 | `kFlangerDepthId` | Depth (0.0-1.0) |
| 1912 | `kFlangerFeedbackId` | Feedback (-1.0 to +1.0) |
| 1913 | `kFlangerMixId` | Mix (0.0-1.0) |
| 1914 | `kFlangerStereoSpreadId` | Stereo Spread (0-360 degrees) |
| 1915 | `kFlangerWaveformId` | Waveform (Sine/Triangle) |
| 1916 | `kFlangerSyncId` | Sync toggle |
| 1917 | `kFlangerNoteValueId` | Note value selector |

Additionally, the modulation type selector needs a new ID. This replaces `kPhaserEnabledId` (1502) functionally, but should be a new ID to avoid confusion:
| 1918 | `kModulationTypeId` | Modulation type (None/Phaser/Flanger) |

**Note**: `kPhaserEnabledId` (1502) remains in the enum for backward compatibility with old automation data but is marked deprecated. The controller registers `kModulationTypeId` as the active parameter.

### R-008: Waveform Selection

**Question**: Should the Flanger use the same LFO waveform enum as the Phaser (4 waveforms: Sine, Triangle, Sawtooth, Square) or a reduced set?

**Decision**: Expose only Sine and Triangle as specified in FR-007. Use the same `LFOWaveform` enum values internally but register only 2 options in the dropdown.

**Rationale**: The spec explicitly states "Sine or Triangle" as the two waveform options. Sine provides a smooth, rounded sweep; Triangle provides the classic linear-ramp flanger character. Square and Sawtooth are less useful for flanging and would add UI clutter.

## Resolved NEEDS CLARIFICATION Items

All technical questions were resolved during the specification clarification session (2026-03-12). No items required further research:

1. Mix topology: True dry/wet crossfade (resolved in spec)
2. Interpolation method: Linear via `DelayLine::readLinear()` (resolved in spec)
3. Crossfade mechanism: Linear ramp, 30ms (resolved in spec)
4. `phaserEnabled_` retirement: Fold into modulation type selector (resolved in spec)
5. Delay sweep range: 0.3ms to 4.0ms (resolved in spec)
