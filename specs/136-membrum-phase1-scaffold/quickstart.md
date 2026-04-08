# Quickstart: Membrum Phase 1

## Build

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure
"$CMAKE" --preset windows-x64-release

# Build plugin + tests
"$CMAKE" --build build/windows-x64-release --config Release --target Membrum membrum_tests
```

## Test

```bash
# Run Membrum unit tests
ctest --test-dir build/windows-x64-release -C Release --output-on-failure -R "membrum_tests"

# Run pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3"
```

## File Layout

```
plugins/membrum/
  CMakeLists.txt
  version.json
  CHANGELOG.md
  src/
    entry.cpp
    plugin_ids.h
    version.h.in
    processor/
      processor.h
      processor.cpp
    controller/
      controller.h
      controller.cpp
    dsp/
      drum_voice.h          # DrumVoice: ImpactExciter + ModalResonatorBank + ADSREnvelope
      membrane_modes.h      # Bessel ratios, mode constants, amplitude computation
  tests/
    CMakeLists.txt
    unit/
      test_main.cpp
      vst/
        membrum_vst_tests.cpp    # Parameter registration, state round-trip, bus config
      processor/
        membrum_processor_tests.cpp  # MIDI handling, audio output, voice behavior
  resources/
    au-info.plist
    auv3/
      audiounitconfig.h
    win32resource.rc.in
  docs/
    index.html
    manual-template.html
    assets/
      style.css
```

## Key Implementation Notes

1. **Single voice**: DrumVoice is instantiated once. No voice allocation.
2. **MIDI note 36 only**: All other notes are silently ignored.
3. **No UI editor**: `Controller::createView()` returns nullptr. Host-generic editor shows the 5 parameters.
4. **Mono to stereo**: Voice output is mono, duplicated to L+R channels.
5. **Parameter changes during playback**: `updateModes()` (not `setModes()`) is used to avoid clearing resonator state.
