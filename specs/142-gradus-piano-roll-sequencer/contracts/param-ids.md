# Contract: Parameter ID Assignments

**Feature**: 142-gradus-piano-roll-sequencer
**File**: `plugins/gradus/src/plugin_ids.h`

This contract specifies the exact parameter IDs added by this feature, their
types, ranges, defaults, and binding requirements.

**Grilling-pass pivot (2026-05-23):** The IDs and metadata below are unchanged, but the **underlying storage** has moved into `ArpeggiatorCore` (`kNumLanes` extended 9→10). Pitches now back `arpCore_.seqNoteLane_` (`ArpLane<uint8_t>`); rest flags back `arpCore_.seqRestFlags_[32]` (atomic array, mirrors MIDI delay's per-step parameter storage pattern). Length/Speed/Swing/Jitter/SpeedCurveDepth/Playhead all reuse the existing lane-modulator infrastructure via the lane-index `9` (zero-based; previously the last index was `8` for MIDI delay).

## Invariants

- All new IDs are ≥ 3741 (next free after `kArpMidiDelayPlayheadId = 3740`).
- New IDs are densely packed: `3741, 3742, ..., 3811` (no gaps).
- The block is **Gradus-exclusive** — these IDs do NOT exist in Ruinae's
  `plugin_ids.h`. Confirmed: Ruinae's arp param block ends at the same
  3000-3372 range it has always occupied; Ruinae's plugin_ids.h does NOT
  define IDs in 3741+ range.
- `kNumParameters` (sentinel, currently 4004) must remain "one past the highest
  used ID". Since 3811 < 4003 (audition param `kAuditionDecayId = 4003`), no
  change to `kNumParameters` is required.

## Allocation Table

| ID | Symbol | Type | Range | Default | UI Hidden | Persist | Notes |
|----|--------|------|-------|---------|-----------|---------|-------|
| 3741 | `kArpSourceModeId` | StringList (2) | [Live, Sequencer] | Live (0) | No | Yes | Top-level source toggle |
| 3742 | `kArpSequencerNoteLaneLengthId` | Range | 1..32 (int) | 16 | No | Yes | Polymetric step count |
| 3743 | `kArpSequencerNoteLaneStep0Id` | Range | 0..127 (int) | 60 | Yes | Yes | Pitch for step 0 |
| 3744..3773 | `kArpSequencerNoteLaneStep1Id`..`kArpSequencerNoteLaneStep30Id` | Range | 0..127 | 60 | Yes | Yes | Pitches for steps 1-30 |
| 3774 | `kArpSequencerNoteLaneStep31Id` | Range | 0..127 | 60 | Yes | Yes | Pitch for step 31 |
| 3775 | `kArpSequencerNoteLaneRestStep0Id` | Toggle | 0..1 (int) | 1 (rest) | Yes | Yes | Rest flag for step 0 |
| 3776..3805 | `kArpSequencerNoteLaneRestStep1Id`..`kArpSequencerNoteLaneRestStep30Id` | Toggle | 0..1 | 1 | Yes | Yes | Rest flags for steps 1-30 |
| 3806 | `kArpSequencerNoteLaneRestStep31Id` | Toggle | 0..1 | 1 | Yes | Yes | Rest flag for step 31 |
| 3807 | `kArpSequencerNoteLaneSpeedId` | Discrete (snapped to `kLaneSpeedValues`) | 0.25x..4.0x | 1.0x | No | Yes | Per-lane speed multiplier |
| 3808 | `kArpSequencerNoteLaneSwingId` | Range | 0..75 (%) | 0 | No | Yes | Per-lane swing |
| 3809 | `kArpSequencerNoteLaneJitterId` | Range | 0..4 (steps) | 0 | No | Yes | Per-lane length jitter |
| 3810 | `kArpSequencerNoteLaneSpeedCurveDepthId` | Range | 0..1 | 0.0 | No | Yes | Per-lane speed curve depth (default 0 = curve off; users opt in by raising) |
| 3811 | `kArpSequencerNoteLanePlayheadId` | Range | 0..1 (normalized) | 0 | Yes | **No** | Output-only (audio → UI) |

End of block: `kArpSequencerNoteLaneEndId = 3811`.

## Contract Requirements

1. **All IDs allocated**: The enum in `plugin_ids.h` MUST contain every symbol
   listed above. Compile-time `static_assert` should verify the sentinel
   relationships:
   ```cpp
   static_assert(kArpSequencerNoteLaneStep31Id == kArpSequencerNoteLaneStep0Id + 31);
   static_assert(kArpSequencerNoteLaneRestStep31Id == kArpSequencerNoteLaneRestStep0Id + 31);
   static_assert(kArpSequencerNoteLaneEndId == 3811);
   static_assert(kArpSequencerNoteLanePlayheadId == kArpSequencerNoteLaneEndId);
   ```

2. **No reuse**: No existing ID may be repurposed. All ID changes are pure
   additions.

3. **Range check in `processParameterChanges`**: The Processor's range-check
   block in `processor.cpp:444-447` MUST include the new IDs:
   ```cpp
   if ((id >= kArpBaseId && id <= kArpEndId) ||
       (id >= kArpVelocityLaneSpeedCurveDepthId && id <= kArpInversionLaneSpeedCurveDepthId) ||
       (id >= kArpMidiDelayLaneLengthId && id <= kArpMidiDelayPlayheadId) ||
       (id >= kArpSourceModeId && id <= kArpSequencerNoteLaneEndId)) {   // NEW
       handleArpParamChange(arpParams_, id, value);
       continue;
   }
   ```

4. **Register all params in Controller**: `registerArpParams()` in
   `arpeggiator_params.h` MUST register all 71 new params with the correct
   metadata (title, units, default, flags). Existing helpers (`createDropdownParameter`,
   `RangeParameter`) MUST be used — no new parameter base classes.

5. **State stream coverage**: `saveArpParams` / `loadArpParams` MUST be extended
   (or a new `saveSequencerNoteLaneParams` / `loadSequencerNoteLaneParams` pair
   added) to serialize all persisted params in order. Playhead (3811) is NOT
   serialized — it's an output-only ephemeral value.

6. **Hidden flag policy**:
   - Per-step pitches and rest flags: HIDDEN (managed via piano roll view; not
     useful to expose as automation lanes on every step individually).
   - Length, speed, swing, jitter, speed-curve depth: VISIBLE (host-automatable
     macro controls).
   - Playhead: HIDDEN (output-only; not user-editable).
   - Source mode: VISIBLE (top-level toggle, host-automatable).

7. **Automation flag**: All new params have `kCanAutomate` (including hidden
   step params — needed for piano-roll → param round-trip via host automation,
   per FR-003 and FR-034).

## Test Coverage

A unit test (`gradus_vst_tests.cpp` extension or new `param_ids_contract_test.cpp`)
MUST verify:
- All listed IDs are defined.
- All listed IDs appear in the parameter container after `Controller::initialize()`.
- No duplicate IDs.
- The static_asserts above hold.
- The new IDs round-trip through `getState` → `setState` correctly.
