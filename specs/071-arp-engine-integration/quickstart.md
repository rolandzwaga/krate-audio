# Quickstart: Arpeggiator Engine Integration (071)

## What This Feature Does

Integrates the ArpeggiatorCore (from spec 070) into the Ruinae synthesizer plugin. When enabled, the arpeggiator intercepts MIDI note-on/off events before they reach the synth engine, transforms held chords into rhythmic note sequences, and routes the resulting events back to the engine's voice allocator. All 11 arp parameters are exposed to the host for automation and saved/recalled with presets. Basic UI controls appear in the SEQ tab.

## Files to Create

| File | Purpose |
|------|---------|
| `plugins/ruinae/src/parameters/arpeggiator_params.h` | Atomic params struct + 6 handler functions |

## Files to Modify

| File | Changes |
|------|---------|
| `plugins/ruinae/src/plugin_ids.h` | Add 11 arp parameter IDs (3000-3010), bump kNumParameters to 3100 |
| `plugins/ruinae/src/processor/processor.h` | Add `arpParams_`, `arpCore_`, `arpEvents_[128]`, `wasTransportPlaying_` |
| `plugins/ruinae/src/processor/processor.cpp` | Modify processEvents, extend applyParamsToEngine, extend getState/setState, add arp block processing in process() |
| `plugins/ruinae/src/controller/controller.h` | Add `arpRateGroup_`, `arpNoteValueGroup_` pointers |
| `plugins/ruinae/src/controller/controller.cpp` | Register arp params, extend formatters, extend state load, add visibility toggle, wire custom views |
| `plugins/ruinae/resources/editor.uidesc` | Split SEQ tab: reduce trance gate, add arp section with 11 controls |

## Files to Create (Tests)

| File | Purpose |
|------|---------|
| `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp` | Parameter round-trip, denormalization, serialization |
| `plugins/ruinae/tests/unit/processor/arp_integration_test.cpp` | MIDI routing, event generation, enable/disable transitions |

## Key Dependencies (Existing, No Changes Needed)

| Component | Location |
|-----------|----------|
| `ArpeggiatorCore` | `dsp/include/krate/dsp/processors/arpeggiator_core.h` |
| `ArpEvent` | `dsp/include/krate/dsp/processors/arpeggiator_core.h` |
| `BlockContext` | `dsp/include/krate/dsp/core/block_context.h` |
| `note_value_ui.h` | `plugins/ruinae/src/parameters/note_value_ui.h` |
| `createNoteValueDropdown()` | `plugins/ruinae/src/controller/parameter_helpers.h` |
| `getNoteValueFromDropdown()` | `dsp/include/krate/dsp/core/note_value.h` |

## Build & Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build
"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests

# Run unit tests
build/windows-x64-release/bin/Release/ruinae_tests.exe

# Pluginval (after full plugin build)
"$CMAKE" --build build/windows-x64-release --config Release
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```

## Implementation Order

1. **Parameter IDs** -- plugin_ids.h (no dependencies)
2. **ArpeggiatorParams** -- arpeggiator_params.h (depends on plugin_ids.h)
3. **Processor integration** -- processor.h/.cpp (depends on params + ArpeggiatorCore)
4. **Controller integration** -- controller.h/.cpp (depends on params)
5. **UI** -- editor.uidesc (depends on controller tags)
6. **Tests** -- unit + integration tests
7. **Pluginval** -- validation pass
