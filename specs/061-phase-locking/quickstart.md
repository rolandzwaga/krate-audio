# Quickstart: Identity Phase Locking for PhaseVocoderPitchShifter

**Feature**: 061-phase-locking | **Date**: 2026-02-17

## What This Feature Does

Identity phase locking improves the audio quality of the `PhaseVocoderPitchShifter` by preserving vertical phase coherence between frequency bins. Without it, the phase vocoder produces a reverberant, smeared quality ("phasiness") on tonal material. With it, pitch-shifted output sounds substantially cleaner and more natural.

## Usage

### Default Behavior (Phase Locking Enabled)

Phase locking is enabled by default. No code changes needed to benefit from improved quality:

```cpp
#include <krate/dsp/processors/pitch_shift_processor.h>

Krate::DSP::PhaseVocoderPitchShifter shifter;
shifter.prepare(44100.0, 512);

// Phase locking is ON by default
float input[512];
float output[512];
shifter.process(input, output, 512, 1.189f);  // +3 semitones, with phase locking
```

### Disabling Phase Locking

To revert to the basic phase vocoder behavior (for A/B comparison or backward compatibility):

```cpp
shifter.setPhaseLocking(false);  // Disable: basic per-bin phase accumulation
shifter.process(input, output, 512, 1.189f);  // Same as pre-modification behavior

// Re-enable at any time
shifter.setPhaseLocking(true);   // Enable: identity phase locking
```

### Querying State

```cpp
bool isLocked = shifter.getPhaseLocking();  // Returns current state (default: true)
```

### With PitchShiftProcessor Wrapper

The `PitchShiftProcessor` wrapper delegates to `PhaseVocoderPitchShifter` when in PhaseVocoder mode. The wrapper's external API is **not modified by this feature** (FR-014) -- there is no `setPhaseLocking()` on the `PitchShiftProcessor` wrapper.

Callers that need phase locking control should use `PhaseVocoderPitchShifter` directly (e.g., the `HarmonizerEngine` in Phase 4 holds instances directly and calls `setPhaseLocking()` on them). No wrapper API change is planned or required.

```cpp
// Direct use (the only supported way to control phase locking):
Krate::DSP::PhaseVocoderPitchShifter shifter;
shifter.prepare(44100.0, 512);
shifter.setPhaseLocking(true);  // controlled directly, not through wrapper
```

## Building

### Windows (MSVC)

```bash
# Configure
"C:/Program Files/CMake/bin/cmake.exe" --preset windows-x64-release

# Build DSP tests
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests

# Run phase locking tests only
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "Phase Locking*"

# Run ALL DSP tests (includes regression check)
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe
```

### Linux/macOS

```bash
cmake --preset linux-release    # or macos-release
cmake --build build/linux-release --config Release --target dsp_tests
build/linux-release/dsp/tests/dsp_tests "Phase Locking*"
```

## Files Involved

| File | Action | Description |
|------|--------|-------------|
| `dsp/include/krate/dsp/processors/pitch_shift_processor.h` | MODIFY | Add phase locking to PhaseVocoderPitchShifter |
| `dsp/tests/unit/processors/phase_locking_test.cpp` | CREATE | Dedicated test suite |
| `dsp/tests/CMakeLists.txt` | MODIFY | Register new test file |
| `specs/_architecture_/layer-2-processors.md` | MODIFY | Document phase locking capability |

## Key Design Decisions

1. **Header-only modification**: No new files for the implementation. All changes are in the existing `pitch_shift_processor.h`.
2. **Enabled by default**: Phase locking is ON by default for maximum quality. Use `setPhaseLocking(false)` to disable.
3. **Two-pass synthesis**: When locked, the synthesis loop runs twice (peaks first, then non-peaks) to ensure peak phases are available for the rotation angle computation.
4. **uint16_t arrays**: Peak indices and region assignments use `uint16_t` (not `size_t`) to reduce cache pressure in the 4-voice harmonizer use case.
5. **Formant compatibility**: The locked phase is stored in `synthPhase_[k]` for all bins, ensuring the formant preservation step works correctly without modification.

## Performance Impact

- **Memory**: +13.3 KB per PhaseVocoderPitchShifter instance (pre-allocated `std::array`)
- **CPU**: Minimal overhead. Peak detection + region assignment are O(numBins). The two-pass synthesis loop has the same sin/cos call count as the basic path. The main benefit is simplified per-bin arithmetic for non-peak bins (shared rotation angle).
- **Latency**: Unchanged (FFT_SIZE + HOP_SIZE = 5120 samples at 44.1 kHz)

## Testing

The test suite covers 8 categories:

1. **Peak Detection**: Correct peak count and positions for sinusoidal and multi-harmonic signals
2. **Region Assignment**: 100% bin coverage, correct midpoint boundaries
3. **Spectral Quality**: Energy concentration in 3-bin window (>= 90% locked vs < 70% basic)
4. **Backward Compatibility**: Sample-accurate match when disabled
5. **Toggle Behavior**: No clicks on locked/basic transitions
6. **Extended Processing**: 10 seconds at various pitch shifts without artifacts
7. **Formant Compatibility**: Correct behavior with both features enabled
8. **Real-Time Safety**: Zero heap allocations, all methods noexcept
