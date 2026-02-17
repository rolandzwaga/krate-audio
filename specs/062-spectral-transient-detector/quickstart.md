# Quickstart: Spectral Transient Detector

**Feature**: 062-spectral-transient-detector

## What This Feature Does

Adds a spectral flux-based transient detector (`SpectralTransientDetector`) as a Layer 1 primitive to KrateDSP, and integrates it with `PhaseVocoderPitchShifter` for transient-aware phase reset. When transients (drum hits, consonant attacks) are detected in the magnitude spectrum, synthesis phases are reset to match analysis phases, preserving transient sharpness instead of smearing them across STFT frames.

## Files to Create

| File | Purpose |
|------|---------|
| `dsp/include/krate/dsp/primitives/spectral_transient_detector.h` | SpectralTransientDetector class (Layer 1 header-only) |
| `dsp/tests/unit/primitives/spectral_transient_detector_test.cpp` | Unit tests for standalone detector |
| `dsp/tests/unit/processors/phase_reset_test.cpp` | Integration tests for phase reset in PhaseVocoderPitchShifter |

## Files to Modify

| File | Change |
|------|--------|
| `dsp/include/krate/dsp/processors/pitch_shift_processor.h` | Add `#include`, member, `setPhaseReset()`/`getPhaseReset()`, integrate into `processFrame()`, `prepare()`, `reset()` |
| `dsp/CMakeLists.txt` | Add `spectral_transient_detector.h` to `KRATE_DSP_PRIMITIVES_HEADERS` |
| `dsp/tests/CMakeLists.txt` | Add both test files to `dsp_tests` target and `-fno-fast-math` list |
| `specs/_architecture_/layer-1-primitives.md` | Document SpectralTransientDetector |
| `specs/_architecture_/layer-2-processors.md` | Document phase reset integration |

## Build & Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run all tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe

# Run only transient detector tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "SpectralTransientDetector*"

# Run only phase reset tests (all test names in phase_reset_test.cpp start with "PhaseReset")
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "PhaseReset*"
```

## Clang-Tidy

Clang-tidy requires the `windows-ninja` build preset (generates `compile_commands.json`).

```powershell
# One-time: generate Ninja build (run from VS Developer PowerShell)
cmake --preset windows-ninja

# Run static analysis on DSP targets
./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja

# Files checked by clang-tidy for this feature:
#   dsp/include/krate/dsp/primitives/spectral_transient_detector.h
#   dsp/include/krate/dsp/processors/pitch_shift_processor.h
#   dsp/tests/unit/primitives/spectral_transient_detector_test.cpp
#   dsp/tests/unit/processors/phase_reset_test.cpp
```

**Note**: The `windows-x64-release` preset used for building tests does NOT produce `compile_commands.json`. Use `windows-ninja` exclusively for clang-tidy runs.

## Usage Example: Standalone

```cpp
#include <krate/dsp/primitives/spectral_transient_detector.h>

Krate::DSP::SpectralTransientDetector detector;
detector.prepare(2049);  // For 4096-point FFT

// In spectral processing loop:
bool isTransient = detector.detect(magnitudeSpectrum, numBins);
if (isTransient) {
    // Handle transient (e.g., phase reset, onset trigger)
}
```

## Usage Example: With PitchShiftProcessor

```cpp
#include <krate/dsp/processors/pitch_shift_processor.h>

Krate::DSP::PitchShiftProcessor shifter;
shifter.prepare(44100.0, 512);
shifter.setMode(Krate::DSP::PitchMode::PhaseVocoder);
shifter.setSemitones(7.0f);
shifter.setPhaseReset(true);   // Enable transient-aware phase reset
shifter.setPhaseLocking(true); // Can be used independently or together

// In audio callback:
shifter.process(input, output, numSamples);
```

## Key Design Decisions

1. **Layer 1 primitive**: No DSP layer dependencies beyond stdlib. Can be used by any spectral processor.
2. **Header-only**: Consistent with other KrateDSP primitives. No .cpp file needed.
3. **Independent toggle**: Phase reset (`setPhaseReset`) is independent of phase locking (`setPhaseLocking`). Both can be enabled simultaneously.
4. **First-frame suppression**: The first `detect()` after `prepare()`/`reset()` always returns `false` to avoid the guaranteed false positive from zero-initialized previous magnitudes.
5. **Default off**: Phase reset is disabled by default in `PhaseVocoderPitchShifter` to maintain backward compatibility.
