# Implementation Plan: WavefolderProcessor

**Branch**: `061-wavefolder-processor` | **Date**: 2026-01-14 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/061-wavefolder-processor/spec.md`

## Summary

WavefolderProcessor is a Layer 2 processor providing full-featured wavefolding with four models (Simple, Serge, Buchla259, Lockhart), parameter smoothing, symmetry control for even harmonics, DC blocking, and dry/wet mix. It builds on existing Layer 0 (WavefoldMath) and Layer 1 (Wavefolder, DCBlocker, OnePoleSmoother) primitives.

**Technical Approach**: Compose existing Layer 1 primitives into a processor following the established TubeStage/DiodeClipper patterns. The Buchla259 model requires a custom 5-stage parallel implementation with Classic/Custom sub-modes.

## Technical Context

**Language/Version**: C++20 (per constitution Principle III)
**Primary Dependencies**:
- Layer 0: `core/wavefold_math.h` (WavefoldMath namespace)
- Layer 1: `primitives/wavefolder.h` (Wavefolder class)
- Layer 1: `primitives/dc_blocker.h` (DCBlocker class)
- Layer 1: `primitives/smoother.h` (OnePoleSmoother class)
- stdlib: `<cstddef>`, `<algorithm>`, `<cmath>`, `<array>`, `<cstdint>`

**Storage**: N/A (stateless aside from smoothers and DC blocker state)
**Testing**: Catch2 (per CLAUDE.md testing-guide skill)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Single header library (header-only Layer 2 processor)
**Performance Goals**: < 2x CPU of TubeStage/DiodeClipper per mono instance (SC-005)
**Constraints**: Real-time safe (no allocations in process()), < 0.5% CPU per instance (Layer 2 budget)
**Scale/Scope**: Mono processor, users instantiate per channel for stereo

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

### Pre-Design Check

**Principle II - Real-Time Audio Thread Safety:**
- [x] No memory allocation in process() path
- [x] No locks, mutexes, or blocking primitives
- [x] No exceptions in audio thread
- [x] Pre-allocated state (smoothers, DC blocker)

**Principle III - Modern C++ Standards:**
- [x] C++20 target with constexpr, noexcept
- [x] RAII for all resources
- [x] No raw new/delete

**Principle IX - Layered DSP Architecture:**
- [x] Layer 2 processor depends only on Layer 0 and Layer 1
- [x] No circular dependencies

**Principle X - DSP Processing Constraints:**
- [x] DC blocking after asymmetric saturation (FR-034)
- [x] No internal oversampling (handled externally per DST-ROADMAP)

**Principle XII - Test-First Development:**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle XIV - ODR Prevention:**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Principle XVI - Honest Completion:**
- [x] All FR-xxx and SC-xxx requirements documented
- [x] Compliance table will be filled with evidence

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: `WavefolderProcessor`, `WavefolderModel` (enum), `BuchlaMode` (enum)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| WavefolderProcessor | `grep -r "WavefolderProcessor" dsp/` | No | Create New |
| WavefolderModel | `grep -r "WavefolderModel" dsp/` | No | Create New |
| BuchlaMode | `grep -r "BuchlaMode" dsp/` | No | Create New |
| Wavefolder | `grep -r "class Wavefolder" dsp/` | Yes - `primitives/wavefolder.h` | REUSE |
| WavefoldType | `grep -r "WavefoldType" dsp/` | Yes - `primitives/wavefolder.h` | REUSE |

**Utility Functions to be created**: None (all math in existing WavefoldMath namespace)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| triangleFold | `grep -r "triangleFold" dsp/` | Yes | `core/wavefold_math.h` | Reuse via Wavefolder |
| sineFold | `grep -r "sineFold" dsp/` | Yes | `core/wavefold_math.h` | Reuse via Wavefolder |
| lambertW | `grep -r "lambertW" dsp/` | Yes | `core/wavefold_math.h` | Reuse via Wavefolder |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| Wavefolder | `primitives/wavefolder.h` | 1 | Core folding for Simple, Serge, Lockhart models |
| WavefoldType | `primitives/wavefolder.h` | 1 | Map WavefolderModel to WavefoldType |
| WavefoldMath::triangleFold | `core/wavefold_math.h` | 0 | Used by Wavefolder primitive |
| WavefoldMath::sineFold | `core/wavefold_math.h` | 0 | Used by Wavefolder primitive |
| WavefoldMath::lambertW | `core/wavefold_math.h` | 0 | Used by Wavefolder primitive |
| DCBlocker | `primitives/dc_blocker.h` | 1 | Remove DC offset after asymmetric folding |
| OnePoleSmoother | `primitives/smoother.h` | 1 | Smooth foldAmount, symmetry, mix |
| FastMath::fastTanh | `core/fast_math.h` | 0 | For Buchla259 stage saturation |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (no WavefolderProcessor)
- [x] `ARCHITECTURE.md` - Component inventory (no WavefolderProcessor)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (WavefolderProcessor, WavefolderModel, BuchlaMode) are unique and not found in codebase. Existing components (Wavefolder, DCBlocker, OnePoleSmoother) will be composed, not duplicated.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| Wavefolder | setType | `void setType(WavefoldType type) noexcept` | Yes |
| Wavefolder | setFoldAmount | `void setFoldAmount(float amount) noexcept` | Yes |
| Wavefolder | process | `[[nodiscard]] float process(float x) const noexcept` | Yes |
| DCBlocker | prepare | `void prepare(double sampleRate, float cutoffHz = 10.0f) noexcept` | Yes |
| DCBlocker | reset | `void reset() noexcept` | Yes |
| DCBlocker | process | `[[nodiscard]] float process(float x) noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | snapToTarget | `void snapToTarget() noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | isComplete | `[[nodiscard]] bool isComplete() const noexcept` | Yes |
| WavefoldMath::triangleFold | triangleFold | `[[nodiscard]] inline float triangleFold(float x, float threshold = 1.0f) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/wavefolder.h` - Wavefolder class, WavefoldType enum
- [x] `dsp/include/krate/dsp/primitives/dc_blocker.h` - DCBlocker class
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/wavefold_math.h` - WavefoldMath namespace
- [x] `dsp/include/krate/dsp/core/fast_math.h` - FastMath::fastTanh
- [x] `dsp/include/krate/dsp/processors/tube_stage.h` - Reference pattern
- [x] `dsp/include/krate/dsp/processors/diode_clipper.h` - Reference pattern

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| OnePoleSmoother | Uses `snapToTarget()` not `snap()` | `smoother.snapToTarget()` |
| OnePoleSmoother | configure() takes (timeMs, sampleRate) not (sampleRate, timeMs) | `smoother.configure(5.0f, 44100.0f)` |
| Wavefolder | setFoldAmount clamps to [0.0, 10.0] and takes abs() | Internal clamping handles range |
| Wavefolder | process() is const - stateless | Can use single instance |
| DCBlocker | Default cutoff is 10Hz in prepare() | `dcBlocker_.prepare(sampleRate, 10.0f)` |
| WavefoldMath::triangleFold | threshold parameter, not gain | `threshold = 1.0 / foldAmount` |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| - | - | - | - |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Buchla259 5-stage parallel fold | Specific to this processor, uses internal state |
| Symmetry offset application | Simple inline operation, not reusable |

**Decision**: No Layer 0 extraction needed. All required math is already in WavefoldMath namespace.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 2 (Processors)

**Related features at same layer** (from DST-ROADMAP):
- TubeStage (059) - Complete, shares parameter smoothing pattern
- DiodeClipper (060) - Complete, shares asymmetry/DC blocking pattern
- FuzzProcessor (future) - Will share saturation/smoothing patterns
- TapeSaturator (future) - Will share gain staging patterns

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Buchla259 5-stage parallel architecture | LOW | None identified | Keep local |
| Parameter smoothing pattern (3 smoothers @ 5ms) | MEDIUM | Already pattern from TubeStage/DiodeClipper | Use established pattern |
| Symmetry -> waveshaper -> DC block -> mix chain | MEDIUM | Similar to TubeStage/DiodeClipper | Use established pattern |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class | Pattern established by TubeStage/DiodeClipper - composition not inheritance |
| Keep Buchla259 implementation local | Unique 5-stage architecture, no other consumers |
| Reuse TubeStage/DiodeClipper patterns | Consistent API, proven patterns |

### Review Trigger

After implementing **FuzzProcessor** (next Layer 2 saturation processor), review this section:
- [ ] Does FuzzProcessor need similar parameter smoothing? -> Already established pattern
- [ ] Does FuzzProcessor use similar signal chain? -> Document shared pattern
- [ ] Any duplicated code? -> Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/061-wavefolder-processor/
├── plan.md              # This file
├── research.md          # Phase 0 output (Buchla259 analysis)
├── data-model.md        # Phase 1 output (class structure)
├── quickstart.md        # Phase 1 output (usage examples)
└── tasks.md             # Phase 2 output (implementation tasks)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── wavefolder_processor.h    # NEW: Layer 2 processor (header-only)
└── tests/
    └── unit/
        └── processors/
            └── wavefolder_processor_test.cpp  # NEW: Unit tests
```

**Structure Decision**: Header-only implementation following TubeStage pattern. Single file at `processors/wavefolder_processor.h` with comprehensive unit tests.

## Complexity Tracking

> No Constitution Check violations requiring justification.

---

## Phase 0: Research

### Research Tasks

1. **Buchla259 5-stage parallel architecture**
   - Confirm fixed Classic values: thresholds {0.2, 0.4, 0.6, 0.8, 1.0}, gains {1.0, 0.8, 0.6, 0.4, 0.2}
   - Determine how foldAmount scales thresholds: threshold_i_scaled = threshold_i / foldAmount
   - Determine output normalization: sum of stages, normalize by gain sum?

2. **Symmetry as DC offset application order**
   - Confirm: symmetry applied before all folding (including Buchla259 parallel stages)
   - DC offset magnitude: symmetry * some_scale (what scale?)

3. **Performance comparison baseline**
   - Measure TubeStage and DiodeClipper CPU at 44.1kHz/512 samples
   - Establish 2x budget for WavefolderProcessor

### Research Findings

#### Buchla259 5-Stage Parallel Architecture

Per spec clarification:
- **Classic mode** uses fixed values:
  - Thresholds: {0.2, 0.4, 0.6, 0.8, 1.0} scaled by 1/foldAmount
  - Gains: {1.0, 0.8, 0.6, 0.4, 0.2}
- **Custom mode** exposes setBuchlaThresholds() and setBuchlaGains() for user-defined values

**Implementation approach:**
```cpp
// Each stage: triangleFold with scaled threshold, then multiply by gain
float output = 0.0f;
for (int i = 0; i < 5; ++i) {
    float scaledThreshold = thresholds_[i] / foldAmount;
    float stageFolded = WavefoldMath::triangleFold(input, scaledThreshold);
    output += stageFolded * gains_[i];
}
// Normalize by sum of gains for consistent output level
float gainSum = gains_[0] + gains_[1] + gains_[2] + gains_[3] + gains_[4];
output /= gainSum;
```

#### Symmetry Application

Per spec clarification (FR-025): Symmetry is applied as DC offset before wavefolding for ALL models including Buchla259:
```cpp
// Apply symmetry as DC offset before folding
float offsetInput = input + symmetry * kSymmetryScale;  // kSymmetryScale TBD
// Then apply selected wavefolder model
```

**Scale factor determination:**
- Looking at TubeStage bias, it maps directly [-1, +1] to waveshaper asymmetry
- For wavefolding, a reasonable DC offset scale would be relative to typical fold threshold
- With foldAmount=1.0 (threshold=1.0), symmetry=1.0 should shift by ~1.0
- Proposed: `offsetInput = input + symmetry * (1.0f / foldAmount)` - scales with fold intensity

#### Performance Baseline

Based on TubeStage/DiodeClipper implementation analysis:
- Both use: 3 smoothers @ 5ms, 1 DC blocker, per-sample processing
- Key difference: WavefolderProcessor Buchla259 mode has 5 parallel folds vs 1 waveshape
- Expected: Buchla259 ~2x single fold, other modes ~1x
- Budget: 2x of reference processors = acceptable

#### Infinity Handling

Per spec clarification: Infinity propagates through (same as NaN):
- Wavefolder primitive already handles infinity per type
- DC blocker will propagate (unbounded output)
- No special handling needed - consistent with existing real-time safety philosophy

---

## Phase 1: Design

### Data Model

See [data-model.md](data-model.md) for complete class structure.

**Key Entities:**

```cpp
// FR-001, FR-002
enum class WavefolderModel : uint8_t {
    Simple = 0,    // Triangle fold
    Serge = 1,     // Sine fold
    Buchla259 = 2, // 5-stage parallel
    Lockhart = 3   // Lambert-W based
};

// FR-002a
enum class BuchlaMode : uint8_t {
    Classic = 0,   // Fixed authentic values
    Custom = 1     // User-configurable
};

class WavefolderProcessor {
public:
    // Constants
    static constexpr float kMinFoldAmount = 0.1f;
    static constexpr float kMaxFoldAmount = 10.0f;
    static constexpr float kDefaultSmoothingMs = 5.0f;
    static constexpr float kDCBlockerCutoffHz = 10.0f;

    // Lifecycle (FR-003, FR-004, FR-005, FR-006)
    WavefolderProcessor() noexcept;
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;

    // Model selection (FR-007, FR-014, FR-023)
    void setModel(WavefolderModel model) noexcept;
    void setBuchlaMode(BuchlaMode mode) noexcept;
    [[nodiscard]] WavefolderModel getModel() const noexcept;

    // Buchla259 custom mode (FR-022b, FR-022c)
    void setBuchlaThresholds(const std::array<float, 5>& thresholds) noexcept;
    void setBuchlaGains(const std::array<float, 5>& gains) noexcept;

    // Parameters (FR-008-013, FR-015-017)
    void setFoldAmount(float amount) noexcept;
    void setSymmetry(float symmetry) noexcept;
    void setMix(float mix) noexcept;
    [[nodiscard]] float getFoldAmount() const noexcept;
    [[nodiscard]] float getSymmetry() const noexcept;
    [[nodiscard]] float getMix() const noexcept;

    // Processing (FR-024-028)
    void process(float* buffer, size_t numSamples) noexcept;

private:
    // Parameters
    WavefolderModel model_ = WavefolderModel::Simple;
    BuchlaMode buchlaMode_ = BuchlaMode::Classic;
    float foldAmount_ = 1.0f;
    float symmetry_ = 0.0f;
    float mix_ = 1.0f;

    // Buchla259 configuration
    std::array<float, 5> buchlaThresholds_ = {0.2f, 0.4f, 0.6f, 0.8f, 1.0f};
    std::array<float, 5> buchlaGains_ = {1.0f, 0.8f, 0.6f, 0.4f, 0.2f};

    // Smoothers (FR-029-033)
    OnePoleSmoother foldAmountSmoother_;
    OnePoleSmoother symmetrySmoother_;
    OnePoleSmoother mixSmoother_;

    // DSP components (FR-037-039)
    Wavefolder wavefolder_;
    DCBlocker dcBlocker_;

    // State
    double sampleRate_ = 44100.0;
    bool prepared_ = false;

    // Internal methods
    [[nodiscard]] float applyBuchla259(float input, float threshold, float foldAmount) const noexcept;
};
```

### API Contracts

See [contracts/wavefolder_processor.h](contracts/wavefolder_processor.h) for detailed API.

**Signal Chain (FR-025):**
```
Input -> [Symmetry DC Offset] -> [Wavefolder (model)] -> [DC Blocker] -> [Mix Blend] -> Output
```

**Processing Loop (per sample):**
```cpp
// 1. Advance smoothers
float foldAmt = foldAmountSmoother_.process();
float sym = symmetrySmoother_.process();
float mixAmt = mixSmoother_.process();

// 2. Check bypass (FR-028)
if (mixAmt < 0.0001f) {
    return;  // Output equals input
}

// 3. Store dry signal
float dry = input;

// 4. Apply symmetry as DC offset (FR-025)
float wet = input + sym * (1.0f / foldAmt);

// 5. Apply selected wavefolder model
switch (model_) {
    case WavefolderModel::Simple:
    case WavefolderModel::Serge:
    case WavefolderModel::Lockhart:
        wavefolder_.setFoldAmount(foldAmt);
        wet = wavefolder_.process(wet);
        break;
    case WavefolderModel::Buchla259:
        wet = applyBuchla259(wet, foldAmt);
        break;
}

// 6. DC blocking (FR-034)
wet = dcBlocker_.process(wet);

// 7. Mix blend
output = dry * (1.0f - mixAmt) + wet * mixAmt;
```

### Quickstart

See [quickstart.md](quickstart.md) for usage examples.

**Basic Usage:**
```cpp
#include <krate/dsp/processors/wavefolder_processor.h>

using namespace Krate::DSP;

WavefolderProcessor folder;
folder.prepare(44100.0, 512);
folder.setModel(WavefolderModel::Serge);
folder.setFoldAmount(3.14159f);  // Characteristic Serge tone
folder.setSymmetry(0.0f);        // Symmetric folding
folder.setMix(1.0f);             // 100% wet

// Process audio
folder.process(buffer, numSamples);
```

**Buchla259 Custom Mode:**
```cpp
WavefolderProcessor folder;
folder.prepare(44100.0, 512);
folder.setModel(WavefolderModel::Buchla259);
folder.setBuchlaMode(BuchlaMode::Custom);
folder.setBuchlaThresholds({0.15f, 0.35f, 0.55f, 0.75f, 0.95f});
folder.setBuchlaGains({1.0f, 0.9f, 0.7f, 0.5f, 0.3f});
```

---

## Post-Design Constitution Re-Check

**Principle II - Real-Time Safety:**
- [x] process() has no allocations - only arithmetic and primitive calls
- [x] No locks or blocking - all state is local
- [x] No exceptions - all methods are noexcept

**Principle III - Modern C++:**
- [x] constexpr constants
- [x] [[nodiscard]] on getters
- [x] noexcept on all methods

**Principle IX - Layer Architecture:**
- [x] Only includes Layer 0 and Layer 1 headers
- [x] No circular dependencies

**Principle X - DSP Constraints:**
- [x] DC blocking after wavefolding (FR-034, FR-035)
- [x] No internal oversampling (per DST-ROADMAP)

**All gates pass. Ready for Phase 2 (task generation via /speckit.tasks).**

---

## Generated Artifacts

| Artifact | Path | Status |
|----------|------|--------|
| Implementation Plan | `specs/061-wavefolder-processor/plan.md` | Complete |
| Research Notes | `specs/061-wavefolder-processor/research.md` | To generate |
| Data Model | `specs/061-wavefolder-processor/data-model.md` | To generate |
| API Contract | `specs/061-wavefolder-processor/contracts/` | To generate |
| Quickstart Guide | `specs/061-wavefolder-processor/quickstart.md` | To generate |

**Branch**: `061-wavefolder-processor`
**Next Step**: Run `/speckit.tasks` to generate implementation task list
