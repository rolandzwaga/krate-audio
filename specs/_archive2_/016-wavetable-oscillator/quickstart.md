# Quickstart: Wavetable Oscillator with Mipmapping

**Branch**: `016-wavetable-oscillator` | **Date**: 2026-02-04

## Files to Create

| File | Purpose |
|------|---------|
| `dsp/include/krate/dsp/core/wavetable_data.h` | Layer 0: WavetableData struct + selectMipmapLevel functions |
| `dsp/include/krate/dsp/primitives/wavetable_generator.h` | Layer 1: Mipmap generation via FFT/IFFT |
| `dsp/include/krate/dsp/primitives/wavetable_oscillator.h` | Layer 1: Playback with cubic Hermite + mipmap crossfade |
| `dsp/tests/unit/core/wavetable_data_test.cpp` | Tests for WavetableData + selectMipmapLevel |
| `dsp/tests/unit/primitives/wavetable_generator_test.cpp` | Tests for generator functions with FFT verification |
| `dsp/tests/unit/primitives/wavetable_oscillator_test.cpp` | Tests for oscillator playback + aliasing |

## Files to Modify

| File | Change |
|------|--------|
| `dsp/CMakeLists.txt` | Add `wavetable_data.h` to KRATE_DSP_CORE_HEADERS, `wavetable_generator.h` and `wavetable_oscillator.h` to KRATE_DSP_PRIMITIVES_HEADERS |
| `dsp/tests/CMakeLists.txt` | Add 3 test files to `dsp_tests` target and `-fno-fast-math` list |
| `specs/_architecture_/layer-0-core.md` | Add WavetableData section |
| `specs/_architecture_/layer-1-primitives.md` | Add WavetableGenerator and WavetableOscillator sections |

## Dependencies

> **Note**: For authoritative dependency API contracts (function signatures, return types, preconditions), see `data-model.md` section "Dependency Contracts".

### wavetable_data.h (Layer 0 -- stdlib only)

```cpp
#include <array>
#include <cmath>      // std::log2f, std::floor
#include <cstddef>    // size_t
```

### wavetable_generator.h (Layer 1)

```cpp
#include <krate/dsp/core/wavetable_data.h>
#include <krate/dsp/core/math_constants.h>  // kPi, kTwoPi
#include <krate/dsp/primitives/fft.h>       // FFT, Complex

#include <algorithm>   // std::max_element
#include <cmath>       // std::abs, std::sqrt
#include <vector>      // temporary buffers (NOT real-time safe)
```

### wavetable_oscillator.h (Layer 1)

```cpp
#include <krate/dsp/core/wavetable_data.h>
#include <krate/dsp/core/interpolation.h>   // cubicHermiteInterpolate, linearInterpolate
#include <krate/dsp/core/phase_utils.h>     // PhaseAccumulator, wrapPhase
#include <krate/dsp/core/math_constants.h>  // kTwoPi
#include <krate/dsp/core/db_utils.h>        // detail::isNaN, detail::isInf

#include <bit>        // std::bit_cast (for NaN detection)
#include <cmath>      // std::floor
#include <cstdint>    // uint32_t
```

> **FTZ/DAZ Note**: Flush-to-zero and denormals-are-zero CPU flags are set at the plugin processor level. The wavetable oscillator does not require anti-denormal measures since it performs only table lookup and interpolation (no feedback loops).

## Build & Test

```bash
# Set CMake alias (Windows)
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure
"$CMAKE" --preset windows-x64-release

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run all DSP tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe

# Run only wavetable tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[WavetableData]"
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[WavetableGenerator]"
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[WavetableOscillator]"
```

## Implementation Order (Test-First)

### Phase 1: WavetableData + Level Selection (Layer 0, US1)

1. Write test: Default construction has kDefaultTableSize=2048 and kMaxMipmapLevels=11 (SC-001 area)
2. Write test: `selectMipmapLevel(20, 44100, 2048)` returns 0 (SC-001)
3. Write test: `selectMipmapLevel(10000, 44100, 2048)` returns 8 (SC-002)
4. Write test: `selectMipmapLevel(0, 44100, 2048)` returns 0 (SC-003)
5. Write test: `selectMipmapLevel(22050, 44100, 2048)` returns highest level (SC-004)
6. Write test: `selectMipmapLevelFractional` returns float values for crossfading (SC-020)
7. Write test: `getLevel()` returns nullptr for invalid level index
8. Implement: WavetableData struct, selectMipmapLevel, selectMipmapLevelFractional
9. Build, verify tests pass

### Phase 2: Standard Waveform Generation (Layer 1, US2)

1. Write test: Generated saw level 0 FFT matches 1/n harmonic series (SC-005)
2. Write test: Generated square level 0 has only odd harmonics (SC-006)
3. Write test: Generated triangle level 0 has 1/n^2 odd harmonics (SC-007)
4. Write test: No harmonics above Nyquist limit for any mipmap level (SC-008)
5. Write test: Highest mipmap level is a sine wave (US2 scenario 5)
6. Write test: Guard samples are correctly set (SC-018)
7. Write test: All values within [-1.05, 1.05] (US2 scenario 4)
8. Implement: generateMipmappedSaw, generateMipmappedSquare, generateMipmappedTriangle
9. Build, verify tests pass

### Phase 3: Custom Harmonic + Raw Sample Generation (Layer 1, US3 + US4)

1. Write test: Fundamental-only spectrum produces sine at all levels (US3 scenario 1)
2. Write test: Custom 4-harmonic spectrum matches within 1% (US3 scenario 2)
3. Write test: 0 harmonics produces silence (edge case)
4. Write test: Sine input produces identical sine at all levels (US4 scenario 1)
5. Write test: Raw saw input matches generateMipmappedSaw within tolerance (US4 scenario 2)
6. Write test: Input size != table size is handled (US4 scenario 3)
7. Implement: generateMipmappedFromHarmonics, generateMipmappedFromSamples
8. Build, verify tests pass

### Phase 4: Oscillator Core Playback (Layer 1, US5)

1. Write test: Sawtooth at 440 Hz produces saw-like output (US5 scenario 1)
2. Write test: Output at 100 Hz matches level 0 table data within interpolation tolerance (US5 scenario 2)
3. Write test: Alias suppression at 10000 Hz >= 50 dB (SC-009)
4. Write test: processBlock matches N sequential process() calls (SC-011)
5. Write test: Null wavetable produces silence (SC-016)
6. Implement: WavetableOscillator prepare/reset/process/processBlock(constant)
7. Build, verify tests pass

### Phase 5: Phase Interface + Modulation (Layer 1, US6)

1. Write test: 440 Hz produces ~440 phase wraps in 44100 samples (SC-010)
2. Write test: resetPhase(0.5) works (SC-012)
3. Write test: PM with 0 radians = unmodulated output (SC-013)
4. Write test: Phase increases monotonically in [0, 1)
5. Write test: phaseWrapped() returns true on wrap
6. Write test: FM buffer processBlock works correctly
7. Implement: phase(), phaseWrapped(), resetPhase(), PM, FM, processBlock(FM)
8. Build, verify tests pass

### Phase 6: Shared Data + Robustness (Layer 1, US7 + Edge Cases)

1. Write test: Two oscillators sharing one WavetableData produce correct independent output (SC-014)
2. Write test: setWavetable(nullptr) mid-stream produces silence safely (US7 scenario 2)
3. Write test: setWavetable(&newTable) mid-stream transitions safely (US7 scenario 3)
4. Write test: NaN/Inf inputs produce safe output (SC-017)
5. Write test: Frequency sweep produces smooth crossfade (SC-020)
6. Write test: processBlock with 0 samples is no-op
7. Implement: edge case handling, output sanitization
8. Build, verify all tests pass

### Phase 7: Cleanup

1. Fix all compiler warnings (zero warnings required per FR-046)
2. Run clang-tidy
3. Update architecture docs
4. Final compliance table review

## Key Formulas

### Mipmap Level Selection (FR-007)

```
level = max(0, floor(log2(frequency * tableSize / sampleRate)))
      = max(0, floor(log2(frequency / fundamentalFreq)))
where fundamentalFreq = sampleRate / tableSize
```

### Fractional Mipmap Level (FR-014a)

```
fracLevel = max(0.0, log2(frequency * tableSize / sampleRate))
clamped to [0.0, numLevels - 1.0]
```

### Max Harmonics per Level

```
maxHarmonic(level) = tableSize / (2^(level + 1))
  Level 0: 1024 harmonics
  Level 1:  512 harmonics
  ...
  Level 10:   1 harmonic (sine)
```

### Sawtooth Harmonic Spectrum (FR-016)

```
For harmonic n = 1 to maxHarmonic:
  spectrum[n] = Complex{0.0f, -1.0f / n}
```

### Square Harmonic Spectrum (FR-017)

```
For odd harmonic n = 1, 3, 5, ... to maxHarmonic:
  spectrum[n] = Complex{0.0f, -1.0f / n}
```

### Triangle Harmonic Spectrum (FR-018)

```
For odd harmonic n = 1, 3, 5, ... to maxHarmonic:
  sign = (((n - 1) / 2) % 2 == 0) ? 1.0f : -1.0f
  spectrum[n] = Complex{0.0f, sign / (n * n)}
```

### Cubic Hermite Table Read (branchless, FR-038)

```cpp
const float* p = table->getLevel(level) + intPhase;
float sample = Interpolation::cubicHermiteInterpolate(p[-1], p[0], p[1], p[2], fracPhase);
```

### Mipmap Crossfade (FR-037)

See `research.md` R-006 for the full crossfade strategy and rationale. Summary:

- Compute `fracLevel = selectMipmapLevelFractional(freq, sr, tableSize)`
- If fractional part < 0.05 or > 0.95: single cubic Hermite lookup (90% of cases)
- Otherwise: two lookups from adjacent levels, linearly blended
- Hysteresis is deferred to future work

### Phase Modulation (FR-042)

```cpp
float pmNormalized = pmOffset_ / kTwoPi;  // radians to [0,1) fraction
double effectivePhase = wrapPhase(phaseAcc_.phase + static_cast<double>(pmNormalized));
// Use effectivePhase for table lookup, do NOT modify phaseAcc_.phase
pmOffset_ = 0.0f;  // Reset after use
```

### FM Buffer processBlock (FR-035a)

```cpp
// Per-sample FM modulation via processBlock overload
WavetableOscillator carrier;
carrier.prepare(44100.0);
carrier.setWavetable(&sawTable);
carrier.setFrequency(440.0f);

// Modulator generates per-sample frequency offsets
float fmBuffer[512];
for (int i = 0; i < 512; ++i) {
    fmBuffer[i] = modulator.process() * fmDepth;  // e.g., ±100 Hz
}

// Process with per-sample FM (mipmap level selected per-sample)
float output[512];
carrier.processBlock(output, fmBuffer, 512);
```

### Output Sanitization (FR-051)

```cpp
// Same pattern as PolyBlepOscillator
float sanitize(float x) noexcept {
    const auto bits = std::bit_cast<uint32_t>(x);
    const bool nan = ((bits & 0x7F800000u) == 0x7F800000u) && ((bits & 0x007FFFFFu) != 0);
    x = nan ? 0.0f : x;
    x = (x < -2.0f) ? -2.0f : x;
    x = (x > 2.0f) ? 2.0f : x;
    return x;
}
```

## Test Helper Usage (for FFT analysis tests)

```cpp
#include <spectral_analysis.h>
#include <krate/dsp/primitives/fft.h>

using namespace Krate::DSP;
using namespace Krate::DSP::TestUtils;

// To verify harmonic content of a generated table level:
FFT fft;
fft.prepare(2048);

const float* levelData = data.getLevel(0);
std::vector<Complex> spectrum(fft.numBins());
fft.forward(levelData, spectrum.data());

// Check magnitude of harmonic n:
float mag = spectrum[n].magnitude();
```

## Architecture Notes

### Why Three Files?

1. **wavetable_data.h (Layer 0)**: Contains only data storage and pure math (mipmap selection). No DSP processing. Layer 0 because it depends only on stdlib. This enables any component at any layer to work with wavetable data.

2. **wavetable_generator.h (Layer 1)**: Contains the generation algorithms that use FFT (Layer 1 dependency). Separate from the oscillator because generation happens once at init time, while playback happens per-sample in real time. This separation enables generating once and sharing across many oscillator instances.

3. **wavetable_oscillator.h (Layer 1)**: Contains the real-time playback engine. Depends only on Layer 0 (no FFT dependency). Could theoretically be Layer 0 since it only uses Layer 0 headers, but placed at Layer 1 as a "primitive" per the roadmap structure and to be a sibling of PolyBlepOscillator.

### Non-Owning Pointer Pattern

`WavetableOscillator` holds `const WavetableData*` (non-owning). This enables:
- Sharing one ~90 KB WavetableData across 16+ polyphonic voices
- Swapping wavetables at runtime (for waveform morphing)
- Clear ownership semantics: the parent component (e.g., synth voice manager) owns the data

The caller is responsible for ensuring the `WavetableData` outlives all oscillators that reference it. In practice, both are typically members of the same parent class.
