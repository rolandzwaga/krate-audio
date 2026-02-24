# Quickstart: Arpeggiator Modulation Integration

**Feature**: 078-modulation-integration
**Date**: 2026-02-24

## Overview

This feature wires 5 arpeggiator parameters into the existing modulation system. No new DSP components are created. The implementation modifies 4 existing files and creates 1 new test file.

## Files to Modify (in order)

### 1. `plugins/ruinae/src/engine/ruinae_engine.h`

**What**: Add 5 enum values + static_assert to `RuinaeModDest`.

**Where**: After line 76 (`AllVoiceFilterEnvAmt = 73`).

**Changes**:
- Add `ArpRate = 74` through `ArpSpice = 78` to the enum
- Add `static_assert(ArpRate == GlobalFilterCutoff + 10, ...)`

**Why**: Assigns DSP-side destination IDs for the ModulationEngine to write offsets to.

### 2. `plugins/shared/src/ui/mod_matrix_types.h`

**What**: Extend UI destination registry from 10 to 15 entries.

**Where**: Line 64 (constant) and lines 161-172 (array).

**Changes**:
- `kNumGlobalDestinations = 15`
- Append 5 entries to `kGlobalDestNames` array
- Array template parameter changes from `10` to `15`

**Why**: Makes the new destinations visible in the mod matrix UI dropdown.

**Validation**: Existing `static_assert` at line 223 catches size mismatches.

### 3. `plugins/ruinae/src/controller/controller.cpp`

**What**: Extend param ID mapping array from 10 to 15 entries.

**Where**: Lines 113-125 (`kGlobalDestParamIds`).

**Changes**:
- Append 5 entries: `kArpFreeRateId, kArpGateLengthId, kArpOctaveRangeId, kArpSwingId, kArpSpiceId`

**Why**: Maps new destination indices to VST parameter IDs for indicator routing.

**Validation**: Existing `static_assert` at line 131 catches size mismatches.

### 4. `plugins/ruinae/src/processor/processor.cpp`

**What**: Read mod offsets and apply to arp parameters in `applyParamsToEngine()`.

**Where**: Lines 1209-1373 (arpeggiator section).

**Changes**:
- Add mod offset reads: `engine_.getGlobalModOffset(RuinaeModDest::ArpRate)` etc.
- Replace raw `arpCore_.setFreeRate(arpParams_.freeRate...)` with modulated version
- Replace raw `arpCore_.setGateLength(...)`, `setSwing(...)`, `setSpice(...)` with modulated versions
- Modify `setOctaveRange()` to use effective (modulated) value in change detection
- Handle tempo-sync rate modulation (compute equivalent free rate from modulated duration)
- Wrap in arp-enabled guard for FR-015 optimization

**Why**: This is the core integration -- applying modulation offsets to arp parameters.

### 5. `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp` (NEW)

**What**: Unit tests for all 5 modulation destinations.

**Pattern**: Same mock infrastructure as `arp_integration_test.cpp`.

**Key test strategy**: Use Macro source (deterministic) to produce known offsets, then verify effective parameter values through observable arp behavior.

## Build & Test Commands

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build
"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests

# Run all tests
build/windows-x64-release/bin/Release/ruinae_tests.exe

# Run only arp mod tests (use Catch2 tag filter matching T010's [arp_mod] tag)
build/windows-x64-release/bin/Release/ruinae_tests.exe "[arp_mod]"

# Build plugin for pluginval
"$CMAKE" --build build/windows-x64-release --config Release

# Run pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```

## Key Design Decisions

1. **Mod offsets read in processor, not engine**: Because arpCore_ is owned by the Processor, not the RuinaeEngine.

2. **Tempo-sync override**: When rate offset != 0 and tempoSync is on, temporarily use free rate mode with the equivalent Hz computed from the modulated step duration.

3. **1-block latency accepted**: Mod offsets from previous block, same as all other global destinations.

4. **FR-015 optimization**: Skip mod reads when arp is disabled.

5. **Change detection uses effective value**: `prevArpOctaveRange_` tracks the modulated value, not the base.

## Common Pitfalls to Avoid

- **Do NOT create new parameter IDs** (FR-019). All routing is through existing mod matrix params.
- **Do NOT modify ArpeggiatorCore** or any DSP library code. All changes are plugin-level.
- **Do NOT modify state serialization**. Existing mod matrix serialization handles new destinations automatically.
- **Do NOT forget the tempoSync override** for rate modulation. Without it, tempo-sync mode ignores the rate offset.
- **Do NOT call setOctaveRange unconditionally**. It resets the selector, breaking the arp pattern.
