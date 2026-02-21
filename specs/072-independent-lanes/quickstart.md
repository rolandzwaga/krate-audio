# Quickstart: 072 Independent Lane Architecture

**Branch**: `072-independent-lanes`
**Date**: 2026-02-21

---

## What This Feature Does

Adds three independent-length step lanes (velocity, gate, pitch) to the ArpeggiatorCore. Each lane cycles at its own rate, producing polymetric arpeggiator patterns. A 3-step velocity pattern over a 5-step gate pattern creates a combined pattern that repeats every 15 steps (LCM).

---

## Files to Create

| File | Layer | Purpose |
|------|-------|---------|
| `dsp/include/krate/dsp/primitives/arp_lane.h` | DSP Layer 1 | ArpLane<T> template container |
| `dsp/tests/unit/primitives/arp_lane_test.cpp` | Test | ArpLane unit tests |

## Files to Modify

| File | Changes |
|------|---------|
| `dsp/include/krate/dsp/processors/arpeggiator_core.h` | Add 3 ArpLane members, lane accessors, resetLanes(), modify fireStep()/reset()/noteOn()/calculateGateDuration() |
| `dsp/tests/unit/processors/arpeggiator_core_test.cpp` | Add lane integration tests, polymetric tests, backward compat test |
| `plugins/ruinae/src/plugin_ids.h` | Add 99 lane parameter IDs (3020-3132), update kArpEndId/kNumParameters |
| `plugins/ruinae/src/parameters/arpeggiator_params.h` | Extend ArpeggiatorParams struct + all 6 functions |
| `plugins/ruinae/src/processor/processor.cpp` | Apply lane params to ArpeggiatorCore in applyParamsToArp() |
| `plugins/ruinae/src/controller/controller.cpp` | Update formatArpParam range check, dispatch lane param formatting |
| `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp` | Add lane parameter tests |
| `specs/_architecture_/layer-1-primitives.md` | Document ArpLane |
| `specs/_architecture_/layer-2-processors.md` | Update ArpeggiatorCore entry |
| `specs/_architecture_/plugin-parameter-system.md` | Document lane parameter IDs |
| `specs/_architecture_/plugin-state-persistence.md` | Document lane serialization |

---

## Build & Test Commands

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run DSP tests (ArpLane + ArpeggiatorCore)
build/windows-x64-release/bin/Release/dsp_tests.exe "[arp_lane]"
build/windows-x64-release/bin/Release/dsp_tests.exe "[arpeggiator_core]"

# Build plugin tests
"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests

# Run plugin tests (arpeggiator params)
build/windows-x64-release/bin/Release/ruinae_tests.exe "[arp_params]"

# Build full plugin for pluginval
"$CMAKE" --build build/windows-x64-release --config Release

# Run pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```

---

## Key Design Decisions

1. **ArpLane is Layer 1 (primitive)**: Simple container, no DSP dependencies. ArpeggiatorCore (Layer 2) composes it.

2. **Template, not inheritance**: `ArpLane<float>` for velocity/gate, `ArpLane<int8_t>` for pitch. No virtual dispatch in audio path.

3. **Lanes advance in fireStep()**: One advance per arp step, not per sample. Ensures exact 1:1 step correspondence.

4. **Bit-identical defaults**: `* 1.0f` and `+ 0` are IEEE 754 identity operations. No special-case branching needed for backward compatibility.

5. **EOF-safe serialization**: Lane data appended after existing arp params. Old presets simply fail to read lane data -> defaults applied.

6. **std::atomic<int> for pitch steps in params**: Avoids lock-free uncertainty with `std::atomic<int8_t>`. Conversion to int8_t happens at the DSP boundary.

---

## Test Strategy

### DSP Layer Tests (dsp_tests)

- ArpLane: construction, length, advance/wrap, reset, setStep/getStep, clamping
- ArpeggiatorCore + lanes: velocity scaling, gate multiplication, pitch offset
- Polymetric: coprime lengths (3,5,7) produce LCM=105 cycle
- Backward compat: all defaults = bit-identical to Phase 3 output
- Reset: retrigger/transport/disable all reset lanes to step 0
- Edge: length change mid-playback, velocity floor of 1, pitch clamping

### Plugin Layer Tests (ruinae_tests)

- Parameter registration: 99 new params registered with correct ranges
- Parameter denormalization: round-trip accuracy for all lane param types
- State serialization: save/load preserves all lane data
- Backward compat: Phase 3 preset loads with lane defaults
- Format display: human-readable values for lane params
