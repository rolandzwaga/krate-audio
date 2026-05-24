# Contract: State Stream v3 Binary Layout

**Feature**: 142-gradus-piano-roll-sequencer
**Files**: `plugins/gradus/src/parameters/arpeggiator_params.h` (`saveArpParams`,
`loadArpParams`), `plugins/gradus/src/processor/processor.cpp` (`getState`,
`setState`).

This contract specifies the exact binary layout of Gradus's state stream after
this feature is implemented (`kCurrentStateVersion = 3`) and the compatibility
guarantees with prior versions.

**Grilling-pass note (2026-05-23):** The state stream layout below is unchanged by the audio-side pivot. The v3 appendix still writes the same 8 fields. The underlying in-memory storage shifted into `ArpeggiatorCore` (lane 10), but `saveArpParams` / `loadArpParams` continue to read from `ArpeggiatorParams` atomics — the core is synced from those atomics at the existing `applyParams` sync point. **Migration test fixtures live at `plugins/gradus/tests/fixtures/gradus_v2_preset_{N}.bin` with paired `gradus_v2_golden_midi_{N}.txt` files** (committed before the version bump on the parent commit); the v2→v3 migration test loads each fixture and asserts byte-identical MIDI output (FR-039b, SC-004).

## Header Format

```
[int32]  version  // 3 (was 2 prior to this feature)
```

Reading code MUST inspect `version` and dispatch:
- `version < 2`: not supported (no Gradus version ever wrote `version < 2`).
- `version == 2`: legacy v2 loader (see "v2 Block" below).
- `version == 3`: v3 loader (v2 block + v3 appendix).
- `version > 3`: future-incompatible — return `kResultFalse` per defensive guard.

## v2 Block (unchanged from current Gradus)

The v2 block contains, in order:

1. Base arp params (11 fields): operatingMode, mode, octaveRange, octaveMode,
   tempoSync, noteValue (int32×6), freeRate, gateLength, swing (float×3),
   latchMode, retrigger (int32×2).
2. Velocity Lane: length (int32) + 32 step floats. EOF-safe.
3. Gate Lane: length (int32) + 32 step floats. EOF-safe.
4. Pitch Lane: length (int32) + 32 step int32. EOF-safe.
5. Modifier Lane: length + 32 steps + accentVelocity + slideTime. EOF-safe at length.
6. Ratchet Lane: length + 32 steps. EOF-safe.
7. Euclidean Timing: enabled, hits, steps, rotation (int32×4). EOF-safe at enabled.
8. Condition Lane: length + 32 steps + fillToggle. EOF-safe at length.
9. Spice / Humanize: 2 floats. EOF-safe.
10. Ratchet Swing: 1 float. EOF-safe.
11. Scale Mode: scaleType, rootNote, scaleQuantizeInput (int32×3). EOF-safe.
12. MIDI Output: midiOut (int32). EOF-safe.
13. Chord Lane: length + 32 steps. EOF-safe.
14. Inversion Lane: length + 32 steps. EOF-safe.
15. Voicing Mode: int32. EOF-safe.
16. Per-lane speed multipliers: 8 floats. EOF-safe.
17. Ratchet Decay + Strum Time + Strum Direction + 8 per-lane Swing values:
    9 floats + 1 int32 + 8 floats. EOF-safe.
18. Velocity Curve + Transpose + 8 per-lane Jitter: 1 int32 + 1 float + 1 int32
    + 8 int32. EOF-safe.
19. Note Range Mapping: rangeLow, rangeHigh, rangeMode (int32×3). EOF-safe.
20. Pin Note + 32 pin flags: int32×33. EOF-safe.
21. Markov: preset (int32) + 49 cells (float). EOF-safe.
22. Per-lane Speed Curve Depth: 8 floats. EOF-safe.
23. Per-lane Speed Curve Point Data: 8 curves × (enabled int32 + presetIndex
    int32 + numPoints int32 + numPoints × 6 floats). EOF-safe.
24. MIDI Delay Lane: length (int32) + 32×7 param arrays + 4 lane modulators.
    EOF-safe.

**End of v2 block.** Total size depends on speed-curve point counts; typical
≈ 4-5 KB.

## v3 Appendix (new)

Written immediately after the MIDI delay lane (item 24 above), in this order:

```
[int32]  sourceMode                     // 0 = Live, 1 = Sequencer
[int32]  seqNoteLaneLength              // 1..32
[int32]  seqNoteLanePitches[32]         // each 0..127
[int32]  seqNoteLaneRestFlags[32]       // each 0..1
[float]  seqNoteLaneSpeed               // 0.25..4.0 (snapped)
[float]  seqNoteLaneSwing               // 0..75
[int32]  seqNoteLaneJitter              // 0..4
[float]  seqNoteLaneSpeedCurveDepth     // 0..1
```

**Total v3 appendix size**: 4 + 4 + 128 + 128 + 4 + 4 + 4 + 4 = **280 bytes**.

**No persisted playhead** — it's runtime-only.

## Read Contract

### v2 Stream Loaded by v3 `setState`

1. Header: `version = 2` read.
2. v2 block consumed in full by `loadArpParams()` — returns `true`.
3. v3 appendix read attempt: `streamer.readInt32(intVal)` for `sourceMode` returns
   `false` (EOF) — `loadSequencerNoteLaneParams()` returns `true`, leaving
   defaults.
4. Defaults applied (per FR-039a):
   - `sourceMode = 0` (Live)
   - `seqNoteLaneLength = 16`
   - All `seqNoteLanePitches[i] = 60`
   - All `seqNoteLaneRestFlags[i] = 1`
   - `seqNoteLaneSpeed = 1.0`
   - `seqNoteLaneSwing = 0.0`
   - `seqNoteLaneJitter = 0`
   - `seqNoteLaneSpeedCurveDepth = 0.0`
5. Result: Gradus runs in Live mode with an empty (all-rest) Sequencer pattern.
6. **MIDI output MUST be byte-identical** to pre-feature Gradus for any held-note
   input over a 60-second test sequence (SC-004, FR-039b).

### v3 Stream Loaded by v3 `setState`

1. Header: `version = 3` read.
2. v2 block consumed by `loadArpParams()`.
3. v3 appendix read by `loadSequencerNoteLaneParams()`:
   ```cpp
   inline bool loadSequencerNoteLaneParams(ArpeggiatorParams& params,
                                            Steinberg::IBStreamer& streamer) {
       int32 intVal = 0;
       float floatVal = 0.0f;

       // EOF-safe at first field
       if (!streamer.readInt32(intVal)) return true;
       params.sourceMode.store(std::clamp(intVal, 0, 1),
                               std::memory_order_relaxed);

       // From here, EOF = corrupt v3 stream → return false
       if (!streamer.readInt32(intVal)) return false;
       params.seqNoteLaneLength.store(std::clamp(intVal, 1, 32),
                                       std::memory_order_relaxed);

       for (int i = 0; i < 32; ++i) {
           if (!streamer.readInt32(intVal)) return false;
           params.seqNoteLanePitches[i].store(std::clamp(intVal, 0, 127),
                                               std::memory_order_relaxed);
       }
       for (int i = 0; i < 32; ++i) {
           if (!streamer.readInt32(intVal)) return false;
           params.seqNoteLaneRestFlags[i].store(intVal != 0 ? 1 : 0,
                                                 std::memory_order_relaxed);
       }
       if (!streamer.readFloat(floatVal)) return false;
       params.seqNoteLaneSpeed.store(std::clamp(floatVal, 0.25f, 4.0f),
                                      std::memory_order_relaxed);
       if (!streamer.readFloat(floatVal)) return false;
       params.seqNoteLaneSwing.store(std::clamp(floatVal, 0.0f, 75.0f),
                                      std::memory_order_relaxed);
       if (!streamer.readInt32(intVal)) return false;
       params.seqNoteLaneJitter.store(std::clamp(intVal, 0, 4),
                                       std::memory_order_relaxed);
       if (!streamer.readFloat(floatVal)) return false;
       params.seqNoteLaneSpeedCurveDepth.store(std::clamp(floatVal, 0.0f, 1.0f),
                                                std::memory_order_relaxed);
       return true;
   }
   ```
4. Result: Gradus restored to the exact saved state.

### v3 Stream Loaded by v2 (pre-feature) Gradus

**Not a supported direction.** Users should not roll Gradus back after upgrading.
If it happens accidentally, the old Gradus will read its v2 block, see the
extra bytes as trailing garbage (silently ignored by `IBStreamer` once
position passes the v2 block), and load with the v2 portion of the state.

## Write Contract

`Processor::getState` writes:
1. `kCurrentStateVersion = 3` (int32).
2. Call `saveArpParams(arpParams_, streamer)` (writes v2 block, unchanged).
3. Call `saveSequencerNoteLaneParams(arpParams_, streamer)` (writes v3 appendix).

```cpp
inline void saveSequencerNoteLaneParams(const ArpeggiatorParams& params,
                                         Steinberg::IBStreamer& streamer) {
    streamer.writeInt32(params.sourceMode.load(std::memory_order_relaxed));
    streamer.writeInt32(params.seqNoteLaneLength.load(std::memory_order_relaxed));
    for (int i = 0; i < 32; ++i) {
        streamer.writeInt32(params.seqNoteLanePitches[i].load(std::memory_order_relaxed));
    }
    for (int i = 0; i < 32; ++i) {
        streamer.writeInt32(params.seqNoteLaneRestFlags[i].load(std::memory_order_relaxed));
    }
    streamer.writeFloat(params.seqNoteLaneSpeed.load(std::memory_order_relaxed));
    streamer.writeFloat(params.seqNoteLaneSwing.load(std::memory_order_relaxed));
    streamer.writeInt32(params.seqNoteLaneJitter.load(std::memory_order_relaxed));
    streamer.writeFloat(params.seqNoteLaneSpeedCurveDepth.load(std::memory_order_relaxed));
}
```

## Test Coverage

### `state_v2_v3_migration_test.cpp` (NEW, in `tests/unit/vst/`)

1. **TEST_CASE "v3 setState handles v2-formatted stream"**:
   - Construct a v2 stream by calling `saveArpParams` with `version = 2` header.
     (Alternative: use a captured byte buffer from a pre-feature build.)
   - Feed to v3 `setState`.
   - Assert `kResultOk` returned.
   - Assert `sourceMode == 0`, `seqNoteLaneLength == 16`,
     `seqNoteLanePitches[*] == 60`, `seqNoteLaneRestFlags[*] == 1`.

2. **TEST_CASE "v3 round-trip preserves all fields"** (FR-039):
   - Set non-default values for every Sequencer Note lane param.
   - Call `getState` → byte buffer.
   - Construct fresh processor; call `setState`.
   - Assert all params match.

3. **TEST_CASE "v2-stream Live mode produces byte-identical MIDI"** (SC-004,
   FR-039b):
   - Construct a representative v2 stream covering all lanes + scale + Markov.
   - Load into v3 processor.
   - Feed a 60-second deterministic MIDI test sequence (e.g., notes 60, 64, 67
     held for 5s each, then chord 60+64+67 for 30s, then arpeggiated rest).
   - Capture output MIDI byte-by-byte.
   - Compare against pre-recorded reference output (from a frozen v2.x Gradus
     build). Tolerance: 0 (byte-identical).

4. **TEST_CASE "v3 setState rejects unknown future versions"**:
   - Write a stream with `version = 999`.
   - Call `setState`.
   - Assert `kResultFalse` returned (defensive guard).

## Invariants

1. **Append-only**: v3 NEVER inserts new fields into the middle of the v2 block.
   All additions are at the tail.
2. **Order stability**: The 8 fields of the v3 appendix are written and read in
   exactly the order shown above. Order MUST NOT change in future versions.
3. **EOF-safety contract**: The FIRST field of any appendix block (sourceMode
   here) MUST be EOF-safe (returning `true` on EOF means "use defaults").
   Subsequent fields within the same appendix block are NOT EOF-safe (returning
   `false` means "corrupt stream").
4. **Defensive future**: If a v4 ever ships, its appendix MUST be appended after
   the v3 appendix; v4's first field MUST be EOF-safe to support v3-stream-loaded-by-v4
   migration.
