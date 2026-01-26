# Implementation Plan: Formant Distortion Processor

**Branch**: `105-formant-distortion` | **Date**: 2026-01-26 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `specs/105-formant-distortion/spec.md`

## Summary

Layer 2 processor composing FormantFilter, Waveshaper, EnvelopeFollower, and DCBlocker to create "talking distortion" effects. The processor applies vowel-shaped formant filtering before waveshaping, with optional envelope-controlled formant modulation for dynamic response. This is a thin composition layer - the heavy lifting is done by existing components.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: FormantFilter (L2), Waveshaper (L1), EnvelopeFollower (L2), DCBlocker (L1), Vowel enum (L0)
**Storage**: N/A (stateless configuration, no persistence in processor)
**Testing**: Catch2 (dsp_tests target)
**Target Platform**: Windows/macOS/Linux (cross-platform VST3)
**Project Type**: DSP Library (monorepo: `dsp/include/krate/dsp/processors/`)
**Performance Goals**: < 0.5% CPU per instance at 44.1kHz (Layer 2 budget)
**Constraints**: Real-time safe (noexcept, no allocations in process), sample-accurate parameter changes
**Scale/Scope**: Single mono processor; stereo via dual instantiation

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle II (Real-Time Safety):**
- [x] All processing methods will be noexcept
- [x] No heap allocations in process() - all buffers pre-allocated in prepare()
- [x] No locks, exceptions, or I/O in audio thread
- [x] Composed components (FormantFilter, Waveshaper, etc.) are all RT-safe

**Principle III (Modern C++):**
- [x] C++20 target with constexpr and RAII
- [x] Value semantics where possible
- [x] Smart pointers not needed (components are value types)

**Principle IX (Layered Architecture):**
- [x] Layer 2 processor - depends on Layers 0-1 only
- [x] EnvelopeFollower is also Layer 2 - allowed as dependency (peer layer)
- [x] Note: Layer 2 CAN use other Layer 2 components

**Principle X (DSP Constraints):**
- [x] DC blocking after waveshaping (DCBlocker composed)
- [x] FormantFilter handles smoothing internally (no double-smoothing)
- [x] No internal oversampling (plugin layer composes if needed)

**Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, dsp-architecture)
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle XIV (ODR Prevention):**
- [x] Codebase Research section complete (see below)
- [x] No duplicate classes/functions will be created

**Principle XVI (Honest Completion):**
- [x] All FR-xxx and SC-xxx requirements will be verified at completion
- [x] Compliance table will be filled with evidence

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: `FormantDistortion`

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| FormantDistortion | `grep -r "class FormantDistortion" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None - all functionality covered by composed components

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| FormantFilter | `dsp/include/krate/dsp/processors/formant_filter.h` | 2 | Vowel/morph filtering, formant shift, internal smoothing |
| Waveshaper | `dsp/include/krate/dsp/primitives/waveshaper.h` | 1 | Saturation with selectable type and drive |
| EnvelopeFollower | `dsp/include/krate/dsp/processors/envelope_follower.h` | 2 | Input envelope tracking for formant modulation |
| DCBlocker | `dsp/include/krate/dsp/primitives/dc_blocker.h` | 1 | Remove DC offset after asymmetric waveshaping |
| Vowel enum | `dsp/include/krate/dsp/core/filter_tables.h` | 0 | Type-safe vowel selection (A, E, I, O, U) |
| WaveshapeType enum | `dsp/include/krate/dsp/primitives/waveshaper.h` | 1 | Distortion algorithm selection |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors
- [x] `specs/_architecture_/` - Component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: FormantDistortion is a new class not found in codebase. All composed components are well-established with stable APIs. No new utility functions needed - composition only.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| FormantFilter | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| FormantFilter | reset | `void reset() noexcept` | Yes |
| FormantFilter | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| FormantFilter | setVowel | `void setVowel(Vowel vowel) noexcept` | Yes |
| FormantFilter | setVowelMorph | `void setVowelMorph(float position) noexcept` | Yes |
| FormantFilter | setFormantShift | `void setFormantShift(float semitones) noexcept` | Yes |
| FormantFilter | setSmoothingTime | `void setSmoothingTime(float ms) noexcept` | Yes |
| FormantFilter | getVowel | `[[nodiscard]] Vowel getVowel() const noexcept` | Yes |
| FormantFilter | getVowelMorph | `[[nodiscard]] float getVowelMorph() const noexcept` | Yes |
| FormantFilter | getFormantShift | `[[nodiscard]] float getFormantShift() const noexcept` | Yes |
| FormantFilter | isInMorphMode | `[[nodiscard]] bool isInMorphMode() const noexcept` | Yes |
| Waveshaper | setType | `void setType(WaveshapeType type) noexcept` | Yes |
| Waveshaper | setDrive | `void setDrive(float drive) noexcept` | Yes |
| Waveshaper | process | `[[nodiscard]] float process(float x) const noexcept` | Yes |
| Waveshaper | getType | `[[nodiscard]] WaveshapeType getType() const noexcept` | Yes |
| Waveshaper | getDrive | `[[nodiscard]] float getDrive() const noexcept` | Yes |
| EnvelopeFollower | prepare | `void prepare(double sampleRate, size_t maxBlockSize) noexcept` | Yes |
| EnvelopeFollower | reset | `void reset() noexcept` | Yes |
| EnvelopeFollower | processSample | `[[nodiscard]] float processSample(float input) noexcept` | Yes |
| EnvelopeFollower | setAttackTime | `void setAttackTime(float ms) noexcept` | Yes |
| EnvelopeFollower | setReleaseTime | `void setReleaseTime(float ms) noexcept` | Yes |
| DCBlocker | prepare | `void prepare(double sampleRate, float cutoffHz = 10.0f) noexcept` | Yes |
| DCBlocker | reset | `void reset() noexcept` | Yes |
| DCBlocker | process | `[[nodiscard]] float process(float x) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/processors/formant_filter.h` - FormantFilter class
- [x] `dsp/include/krate/dsp/primitives/waveshaper.h` - Waveshaper class, WaveshapeType enum
- [x] `dsp/include/krate/dsp/processors/envelope_follower.h` - EnvelopeFollower class
- [x] `dsp/include/krate/dsp/primitives/dc_blocker.h` - DCBlocker class
- [x] `dsp/include/krate/dsp/core/filter_tables.h` - Vowel enum, FormantData struct
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother (for mix smoothing)

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| FormantFilter | Uses `setVowelMorph()` not `setVowelBlend()` | `formantFilter_.setVowelMorph(position)` |
| FormantFilter | Morph range is [0.0, 4.0] not [0.0, 1.0] | Spec FR-006 aligns with this |
| Waveshaper | Drive is linear multiplier, not dB | `setDrive(2.0f)` = 2x gain |
| Waveshaper | Drive of 0.0 returns 0.0 | Ensure drive >= 0.5 per FR-013 |
| EnvelopeFollower | Uses `processSample()` not `process(float)` | `env.processSample(input)` |
| EnvelopeFollower | prepare() takes maxBlockSize parameter | `prepare(sampleRate, maxBlockSize)` |
| DCBlocker | Returns input unchanged if not prepared | Always call prepare() first |
| OnePoleSmoother | Uses `snapTo()` for immediate value set | `smoother.snapTo(value)` |

## Layer 0 Candidate Analysis

*No new Layer 0 utilities needed. All functionality is composition of existing components.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| - | - | - | - |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Envelope-to-shift calculation | Simple inline formula: `staticShift + (envelope * modRange * amount)` |

**Decision**: No extraction needed. The envelope modulation formula is trivial and specific to this processor.

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer**:
- Other saturation processors (TubeStage, DiodeClipper) - different signal chains
- FormantFilter standalone - already exists, this extends it
- Future vocal processors - could share envelope-to-formant pattern

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| FormantDistortion | LOW | None identified | Keep local |
| Envelope-to-formant modulation | MEDIUM | VocoderDistortion (hypothetical) | Keep local, extract if 2nd use |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class | Each processor has unique signal chain; composition is the pattern |
| Keep envelope modulation inline | Simple formula, no clear second consumer yet |

## Project Structure

### Documentation (this feature)

```text
specs/105-formant-distortion/
├── plan.md              # This file
├── research.md          # Phase 0 output (N/A - research complete)
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
└── tasks.md             # Phase 2 output (/speckit.tasks command)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── formant_distortion.h    # NEW: FormantDistortion class (header-only)
└── tests/
    └── processors/
        └── formant_distortion_test.cpp  # NEW: Unit tests

# Updates to existing files:
specs/_architecture_/layer-2-processors.md  # Add FormantDistortion documentation
```

**Structure Decision**: Header-only implementation in Layer 2 processors directory. No .cpp file needed - pattern matches FormantFilter. Tests in parallel test directory structure.

## Complexity Tracking

> No Constitution violations - all requirements can be met with existing patterns.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| - | - | - |

---

## Phase 0: Research (Complete)

All clarifications resolved in spec.md Session 2026-01-26:

1. **Envelope modulation direction**: Unipolar positive - `finalShift = staticShift + (envelope * modRange * amount)`
2. **Mix location**: Post-DC blocker, inside FormantDistortion
3. **Parameter smoothing**: Delegate to FormantFilter's internal smoothing
4. **Vowel mode state**: Independent parameters with mode flag
5. **Envelope tracking point**: Raw input (before any processing)

No additional research needed. All composed components have verified APIs.

---

## Phase 1: Design

### Signal Flow

```
Raw Input ─────────────────┬──────────────────────────────────────> Dry Signal
                           │                                            │
                           v                                            │
                    EnvelopeFollower                                    │
                           │                                            │
                           v                                            │
                  Calculate finalShift                                  │
                  = staticShift + (env * modRange * amount)             │
                           │                                            │
                           v                                            │
                    FormantFilter ◄──── setFormantShift(finalShift)     │
                           │                                            │
                           v                                            │
                      Waveshaper                                        │
                           │                                            │
                           v                                            │
                      DCBlocker                                         │
                           │                                            │
                           v                                            v
                      Wet Signal ──────────> Mix Stage <──────────> Dry Signal
                                               │
                                               v
                                            Output
```

### Class Design

```cpp
namespace Krate::DSP {

class FormantDistortion {
public:
    // Constants
    static constexpr float kMinDrive = 0.5f;
    static constexpr float kMaxDrive = 20.0f;
    static constexpr float kMinShift = -24.0f;
    static constexpr float kMaxShift = 24.0f;
    static constexpr float kMinEnvModRange = 0.0f;
    static constexpr float kMaxEnvModRange = 24.0f;
    static constexpr float kDefaultEnvModRange = 12.0f;
    static constexpr float kDefaultSmoothingMs = 5.0f;

    // Lifecycle (FR-001, FR-002)
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;

    // Processing (FR-003, FR-004, FR-028, FR-029)
    void process(float* buffer, size_t numSamples) noexcept;
    [[nodiscard]] float process(float sample) noexcept;

    // Vowel Selection (FR-005, FR-006, FR-007, FR-008)
    void setVowel(Vowel vowel) noexcept;
    void setVowelBlend(float blend) noexcept;  // Maps to FormantFilter::setVowelMorph

    // Formant Modification (FR-009, FR-010, FR-011)
    void setFormantShift(float semitones) noexcept;

    // Distortion (FR-012, FR-013, FR-014)
    void setDistortionType(WaveshapeType type) noexcept;
    void setDrive(float drive) noexcept;

    // Envelope Following (FR-015, FR-016, FR-017, FR-018)
    void setEnvelopeFollowAmount(float amount) noexcept;
    void setEnvelopeModRange(float semitones) noexcept;
    void setEnvelopeAttack(float ms) noexcept;
    void setEnvelopeRelease(float ms) noexcept;

    // Smoothing (FR-024, FR-025)
    void setSmoothingTime(float ms) noexcept;  // Pass-through to FormantFilter

    // Mix (FR-026, FR-027)
    void setMix(float mix) noexcept;

    // Getters (FR-030)
    [[nodiscard]] Vowel getVowel() const noexcept;
    [[nodiscard]] float getVowelBlend() const noexcept;
    [[nodiscard]] float getFormantShift() const noexcept;
    [[nodiscard]] WaveshapeType getDistortionType() const noexcept;
    [[nodiscard]] float getDrive() const noexcept;
    [[nodiscard]] float getEnvelopeFollowAmount() const noexcept;
    [[nodiscard]] float getMix() const noexcept;

private:
    // Composed components
    FormantFilter formantFilter_;
    Waveshaper waveshaper_;
    EnvelopeFollower envelopeFollower_;
    DCBlocker dcBlocker_;
    OnePoleSmoother mixSmoother_;

    // Parameters
    Vowel vowel_ = Vowel::A;
    float vowelBlend_ = 0.0f;
    bool useBlendMode_ = false;
    float staticFormantShift_ = 0.0f;
    float envelopeFollowAmount_ = 0.0f;
    float envelopeModRange_ = kDefaultEnvModRange;
    float mix_ = 1.0f;

    // State
    double sampleRate_ = 44100.0;
    bool prepared_ = false;
};

} // namespace Krate::DSP
```

### Test Strategy

1. **Unit Tests** (`formant_distortion_test.cpp`):
   - Basic lifecycle (prepare, reset)
   - Parameter setters/getters with clamping
   - Vowel selection modes (discrete vs blend)
   - Mix behavior (0 = dry, 1 = wet, 0.5 = blend)

2. **Audio Tests**:
   - Formant peak verification (SC-001): Process white noise, verify spectral peaks at F1/F2/F3
   - Click-free transitions (SC-002): Automate vowel blend, check for discontinuities
   - Envelope response (SC-003): Process impulse, verify attack timing
   - THD increase with drive (SC-006): Compare harmonics at drive=1 vs drive=4
   - DC blocking (SC-008): Verify < -60dB DC after asymmetric distortion

3. **Performance Test**:
   - CPU usage benchmark (SC-004): < 0.5% at 44.1kHz

### File Deliverables

| File | Purpose |
|------|---------|
| `dsp/include/krate/dsp/processors/formant_distortion.h` | Header-only implementation |
| `dsp/tests/processors/formant_distortion_test.cpp` | Catch2 unit tests |
| `specs/_architecture_/layer-2-processors.md` | Architecture documentation update |

---

## Verification Checklist (Post-Implementation)

*To be filled during implementation:*

- [ ] All FR-xxx requirements implemented
- [ ] All SC-xxx success criteria tested
- [ ] Zero compiler warnings
- [ ] All tests pass
- [ ] Architecture docs updated
- [ ] Compliance table in spec.md filled
