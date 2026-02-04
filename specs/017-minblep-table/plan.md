# Implementation Plan: MinBLEP Table

**Branch**: `017-minblep-table` | **Date**: 2026-02-04 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/017-minblep-table/spec.md`

## Summary

Implement a precomputed minimum-phase band-limited step function (minBLEP) table at Layer 1 (`dsp/include/krate/dsp/primitives/minblep_table.h`) in the `Krate::DSP` namespace. The table is generated once at init-time via a multi-step algorithm (windowed sinc -> cepstral minimum-phase transform -> integration -> normalization), stored as an oversampled polyphase table for real-time sub-sample-accurate lookup, and consumed through a nested `Residual` ring buffer that applies band-limited corrections to oscillator output at discontinuity points. This is Phase 4 of the oscillator roadmap, providing the foundation for the Sync Oscillator (Phase 5) and Sub-Oscillator (Phase 6).

## Technical Context

**Language/Version**: C++20 (MSVC, Clang, GCC)
**Primary Dependencies**: `primitives/fft.h` (FFT class, Complex struct), `core/window_functions.h` (Blackman window), `core/interpolation.h` (linear interpolation), `core/math_constants.h` (kPi), `core/db_utils.h` (NaN/Inf detection)
**Storage**: In-memory `std::vector<float>` (polyphase table ~4KB for default params)
**Testing**: Catch2 unit tests at `dsp/tests/unit/primitives/minblep_table_test.cpp` *(Constitution Principle XIII: Test-First Development)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform header-only
**Project Type**: Shared DSP library (monorepo: `dsp/`)
**Performance Goals**: `sample()` and `consume()` < 0.1% CPU per Layer 1 budget; `prepare()` runs once at init-time (no real-time constraint)
**Constraints**: Zero allocation in `sample()`, `consume()`, `addBlep()`, `reset()`. No exceptions, no blocking, no I/O on real-time code paths.
**Scale/Scope**: Single header file + test file. ~200-300 lines of implementation.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-Design Check (PASSED):**

**Required Check - Principle II (Real-Time Safety):**
- [x] `sample()`, `consume()`, `addBlep()`, `reset()` are noexcept, no allocation, no exceptions, no blocking
- [x] `prepare()` is explicitly NOT real-time safe (allocates memory, performs FFT)
- [x] All buffers pre-allocated in `prepare()` or `Residual` constructor

**Required Check - Principle III (Modern C++):**
- [x] C++20 features: `[[nodiscard]]`, `std::has_single_bit()`, `std::bit_cast()` (if needed)
- [x] RAII via `std::vector` for table and ring buffer storage
- [x] No raw `new`/`delete`

**Required Check - Principle IX (Layer Architecture):**
- [x] Layer 1 primitive: depends only on Layer 0 (`core/`) and Layer 1 (`primitives/fft.h`)
- [x] No dependencies on Layer 2 or higher
- [x] No circular dependencies

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Post-Design Re-Check (PASSED):**
- [x] API contract in `contracts/minblep_table.h` respects all constitution principles
- [x] Data model stores no mutable state accessed from multiple threads
- [x] No violations requiring justification

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: `MinBlepTable`, `MinBlepTable::Residual`

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| `MinBlepTable` | `grep -r "class MinBlepTable" dsp/ plugins/` | No | Create New |
| `MinBlepTable` | `grep -r "MinBlepTable" dsp/ plugins/` | No | Create New |
| `Residual` (struct) | `grep -r "struct Residual" dsp/ plugins/` | No (variable named `residual` exists in `fractal_distortion.h` but not a type) | Create New (nested in MinBlepTable, no conflict) |
| `MinBlep` | `grep -r "MinBlep\|minblep\|MINBLEP" dsp/ plugins/` | No | Clean namespace |
| `BLIT` / `blit` | `grep -r "BLIT\|blit" dsp/ plugins/` | No | Clean namespace |

**Utility Functions to be created**: None (all inline methods within the class)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| (no standalone functions) | N/A | N/A | N/A | N/A |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `FFT` class | `dsp/include/krate/dsp/primitives/fft.h` | 1 | Forward/inverse FFT for minimum-phase transform in `prepare()` |
| `Complex` struct | `dsp/include/krate/dsp/primitives/fft.h` | 1 | FFT bin manipulation during cepstral transform |
| `Window::generateBlackman()` | `dsp/include/krate/dsp/core/window_functions.h` | 0 | Blackman window for sinc windowing step |
| `Interpolation::linearInterpolate()` | `dsp/include/krate/dsp/core/interpolation.h` | 0 | Sub-sample interpolation in `sample()` method |
| `kPi` | `dsp/include/krate/dsp/core/math_constants.h` | 0 | Sinc function computation `sin(pi*x)/(pi*x)` |
| `detail::isNaN()` | `dsp/include/krate/dsp/core/db_utils.h` | 0 | NaN detection in `addBlep()` amplitude check (FR-037) |
| `detail::isInf()` | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Infinity detection in `addBlep()` amplitude check (FR-037) |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (no conflicts)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (no `MinBlepTable`, no `Residual` type)
- [x] `specs/_architecture_/` - Component inventory (no minBLEP entry)
- [x] `dsp/include/krate/dsp/core/polyblep.h` - PolyBLEP math (different concept, no conflict)
- [x] `dsp/include/krate/dsp/primitives/polyblep_oscillator.h` - PolyBLEP oscillator (complementary, no conflict)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (`MinBlepTable`, `MinBlepTable::Residual`) are unique and do not exist anywhere in the codebase. The `Residual` struct is nested within `MinBlepTable`, so its fully qualified name `Krate::DSP::MinBlepTable::Residual` has no possibility of collision with the `residual` variable in `fractal_distortion.h` (which is a local variable, not a type).

## Dependency API Contracts (Principle XV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| `FFT` | `prepare` | `void prepare(size_t fftSize) noexcept` | Yes |
| `FFT` | `forward` | `void forward(const float* input, Complex* output) noexcept` | Yes |
| `FFT` | `inverse` | `void inverse(const Complex* input, float* output) noexcept` | Yes |
| `FFT` | `size` | `[[nodiscard]] size_t size() const noexcept` | Yes |
| `FFT` | `numBins` | `[[nodiscard]] size_t numBins() const noexcept` | Yes |
| `FFT` | `isPrepared` | `[[nodiscard]] bool isPrepared() const noexcept` | Yes |
| `Complex` | `real` | `float real = 0.0f` | Yes |
| `Complex` | `imag` | `float imag = 0.0f` | Yes |
| `Complex` | `magnitude` | `[[nodiscard]] float magnitude() const noexcept` | Yes |
| `Window` | `generateBlackman` | `inline void generateBlackman(float* output, size_t size) noexcept` | Yes |
| `Interpolation` | `linearInterpolate` | `[[nodiscard]] constexpr float linearInterpolate(float y0, float y1, float t) noexcept` | Yes |
| `kPi` | constant | `inline constexpr float kPi = 3.14159265358979323846f` | Yes |
| `detail` | `isNaN` | `constexpr bool isNaN(float x) noexcept` | Yes |
| `detail` | `isInf` | `constexpr bool isInf(float x) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/fft.h` - FFT class and Complex struct
- [x] `dsp/include/krate/dsp/core/window_functions.h` - Window namespace and generateBlackman
- [x] `dsp/include/krate/dsp/core/interpolation.h` - Interpolation namespace and linearInterpolate
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi, kTwoPi constants
- [x] `dsp/include/krate/dsp/core/db_utils.h` - detail::isNaN, detail::isInf functions

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| `FFT::forward()` | Takes `const float*` input, `Complex*` output; output size is `numBins()` = N/2+1 | Allocate N/2+1 Complex elements for output |
| `FFT::inverse()` | Takes `const Complex*` input (N/2+1 bins), `float*` output (N samples); expects conjugate symmetry | Ensure spectrum is conjugate-symmetric before calling |
| `FFT::inverse()` | Output is scaled by `1/N` internally | No additional scaling needed after inverse |
| `FFT` | Min size 256, max 8192; requires power-of-2 | Use `std::bit_ceil()` to compute next power-of-2 |
| `Window::generateBlackman()` | Uses periodic (DFT-even) variant: divides by N, not N-1 | Fine for sinc windowing where N is the sinc length |
| `Interpolation::linearInterpolate()` | Parameters are `(y0, y1, t)` not `(a, b, fraction)` | `linearInterpolate(tableVal0, tableVal1, fracOffset)` |
| `detail::isNaN()` | In `detail` namespace inside `Krate::DSP`, accessed as `detail::isNaN()` | Must include `<krate/dsp/core/db_utils.h>` |

## Layer 0 Candidate Analysis

*This is a Layer 1 feature. No new Layer 0 utilities are needed.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| None | All needed utilities already exist in Layer 0 | N/A | N/A |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Windowed sinc generation | Only used within `prepare()`, tightly coupled to table generation |
| Integration (cumulative sum) | One-liner used only in `prepare()` |
| Cepstral minimum-phase transform | Complex multi-step algorithm specific to minBLEP generation |
| Normalization | Specific to minBLEP table constraints |

**Decision**: No extraction needed. All new code is either part of the `prepare()` algorithm (init-time only) or part of the `sample()`/`Residual` real-time interface. The minimum-phase transform could theoretically be extracted, but there is only one consumer; defer until a second use case emerges.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 1 - DSP Primitives

**Related features at same layer** (from OSC-ROADMAP.md):
- `primitives/polyblep_oscillator.h` (Phase 3, already exists) - PolyBLEP-based anti-aliasing for continuous discontinuities
- `primitives/wavetable_oscillator.h` (Phase 3, already exists) - Mipmapped wavetable playback
- Phase 5: Sync Oscillator (Layer 2 processor, will compose MinBlepTable)
- Phase 6: Sub-Oscillator (Layer 2 processor, may use MinBlepTable for clean flip-flop transitions)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `MinBlepTable` class | HIGH | Sync Oscillator (Phase 5), Sub-Oscillator (Phase 6) | Keep at Layer 1 for composition |
| `MinBlepTable::Residual` | HIGH | Any oscillator with hard discontinuities | Keep as nested struct for API clarity |
| Minimum-phase transform algorithm | LOW | No other known consumer | Keep inside `prepare()` |

### Detailed Analysis (for HIGH potential items)

**MinBlepTable + Residual** provides:
- Pre-computed polyphase minBLEP lookup with sub-sample accuracy
- Ring buffer for "fire and forget" correction stamping
- Configurable quality (oversampling factor, zero crossings)

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| Sync Oscillator (Phase 5) | YES | Primary consumer. One shared table per voice group, one Residual per voice. |
| Sub-Oscillator (Phase 6) | MAYBE | Square sub-osc flip-flops could use minBLEP for clean transitions. |
| FM Operator (Phase 8) | NO | FM uses continuous modulation, not hard discontinuities. |

**Recommendation**: Keep `MinBlepTable` and `Residual` in `primitives/minblep_table.h`. The Sync Oscillator (Phase 5) will compose these at Layer 2.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep Residual nested in MinBlepTable | API clarity: consumers always see `MinBlepTable::Residual`, making the relationship explicit |
| Non-owning pointer pattern for Residual | Matches WavetableOscillator -> WavetableData pattern; caller manages lifetime |
| Header-only implementation | Matches all other Layer 0/1 components in the codebase |

### Review Trigger

After implementing **Phase 5 (Sync Oscillator)**, review this section:
- [ ] Does Sync Oscillator compose MinBlepTable + Residual as expected?
- [ ] Does the Residual ring buffer capacity (= table length) suffice for typical sync ratios?
- [ ] Any API friction points that need adjustment?

## Project Structure

### Documentation (this feature)

```text
specs/017-minblep-table/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
│   └── minblep_table.h  # API contract
├── checklists/          # Pre-existing
│   └── requirements.md
└── tasks.md             # Phase 2 output (NOT created by plan)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── primitives/
│       └── minblep_table.h          # NEW: MinBlepTable + Residual implementation
└── tests/
    └── unit/primitives/
        └── minblep_table_test.cpp   # NEW: Catch2 unit tests
```

**Files to modify:**
- `dsp/tests/CMakeLists.txt` - Add test file to `dsp_tests` target and `-fno-fast-math` list
- `specs/_architecture_/layer-1-primitives.md` - Add MinBlepTable documentation section

**Structure Decision**: Standard header-only DSP primitive pattern. Single header in `primitives/`, single test file in `tests/unit/primitives/`. No source files needed (all inline).

## Complexity Tracking

> No violations. All constitution gates pass without exception.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| (none) | N/A | N/A |
