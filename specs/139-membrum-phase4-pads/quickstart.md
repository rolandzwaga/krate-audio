# Quickstart: Membrum Phase 4

## Overview

Phase 4 transforms Membrum from a single-template drum synth into a full 32-pad drum machine. Each pad (MIDI 36-67) gets independent exciter/body/parameter configuration, with kit presets, per-pad presets, and 16 stereo output buses.

## Key Concepts

### PadConfig

A pre-allocated struct holding one pad's complete 32-parameter configuration. 32 instances are stored in VoicePool. When a MIDI note arrives, the voice pool looks up `padConfigs_[note - 36]` and configures the voice accordingly.

### Per-Pad Parameter IDs

Computed as `1000 + padIndex * 64 + offset`. Pad 0 uses IDs 1000-1031 (active) with 1032-1063 reserved. The stride of 64 leaves room for future per-pad parameters (Phase 5 coupling, etc.).

### Selected Pad Proxy

The existing global parameter IDs (100-252) become "proxy" parameters that redirect to whichever pad is currently selected via `kSelectedPadId` (260). This makes the host-generic editor usable without displaying all 1024 per-pad parameters simultaneously.

### Multi-Bus Output

16 stereo output buses: 1 main (always active) + 15 auxiliary (host-activated). Every pad's audio goes to the main bus. Pads assigned to an auxiliary bus also go to that bus. This is a "send" model, not exclusive routing.

## Build & Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build
"$CMAKE" --build build/windows-x64-release --config Release --target Membrum

# Run tests
"$CMAKE" --build build/windows-x64-release --config Release --target membrum_tests
build/windows-x64-release/bin/Release/membrum_tests.exe

# Pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3"
```

## Files to Modify

### New Files
- `plugins/membrum/src/dsp/pad_config.h` -- PadConfig struct + constants + helpers
- `plugins/membrum/src/dsp/default_kit.h` -- GM-inspired default templates
- `plugins/membrum/src/preset/membrum_preset_config.h` -- Kit + pad preset configs

### Modified Files
- `plugins/membrum/src/plugin_ids.h` -- Add kSelectedPadId, kPadBaseId, bump state version to 4
- `plugins/membrum/src/processor/processor.h` -- Add busActive_ array, remove individual atomics for per-pad params
- `plugins/membrum/src/processor/processor.cpp` -- Rewrite processParameterChanges for per-pad dispatch, rewrite getState/setState for v4, add activateBus override, multi-bus process()
- `plugins/membrum/src/voice_pool/voice_pool.h` -- Replace SharedParams with PadConfig[32], extend processBlock signature
- `plugins/membrum/src/voice_pool/voice_pool.cpp` -- Per-pad noteOn dispatch, multi-bus processBlock, remove setShared* methods
- `plugins/membrum/src/controller/controller.h` -- Add per-pad proxy logic
- `plugins/membrum/src/controller/controller.cpp` -- Register 1024 per-pad params, add proxy forwarding
- `plugins/membrum/CMakeLists.txt` -- Add new source files
- `plugins/membrum/resources/au-info.plist` -- Multi-output AU config
- `plugins/membrum/resources/auv3/audiounitconfig.h` -- Multi-output AU config

### Test Files (new)
- `tests/unit/vst/test_pad_config.cpp` -- PadConfig struct, ID computation, defaults
- `tests/unit/vst/test_state_v4.cpp` -- v4 save/load round-trip
- `tests/unit/vst/test_state_migration_v3_to_v4.cpp` -- v3 -> v4 migration
- `tests/unit/vst/test_pad_parameters.cpp` -- Per-pad parameter registration and proxy
- `tests/unit/voice_pool/test_per_pad_dispatch.cpp` -- Voice pool uses pad config at noteOn
- `tests/unit/voice_pool/test_multi_bus_output.cpp` -- Multi-bus routing
- `tests/unit/processor/test_default_kit.cpp` -- GM default templates
- `tests/unit/preset/test_kit_preset.cpp` -- Kit preset save/load
- `tests/unit/preset/test_pad_preset.cpp` -- Per-pad preset save/load

## Implementation Order

1. PadConfig struct + constants (foundation for everything else)
2. Default kit templates
3. VoicePool per-pad dispatch (replace SharedParams)
4. Processor per-pad parameter handling
5. State v4 serialization + migration
6. Multi-bus output (processor + voice pool)
7. Controller per-pad parameter registration + proxy logic
8. Kit preset integration
9. Per-pad preset integration
10. AU configuration update
11. Factory kit presets
12. Pluginval + final validation
