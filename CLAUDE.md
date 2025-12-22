# CLAUDE.md - VST Plugin Development Guidelines

This file provides guidance for AI assistants working on this VST3 plugin project. All code contributions must comply with the project constitution at `.specify/memory/constitution.md`.

## Project Overview

This is a **VST3 plugin** built with:
- **Steinberg VST3 SDK** (not JUCE or other frameworks)
- **VSTGUI** for user interface
- **Modern C++20**
- **CMake 3.20+** build system

## Critical Rules (Non-Negotiable)

### 1. Real-Time Audio Thread Safety

The audio thread (`Processor::process()`) has **hard real-time constraints**. The following are **FORBIDDEN** in any code path called from `process()`:

```cpp
// NEVER DO THESE IN AUDIO THREAD:
new / delete / malloc / free          // Memory allocation
std::vector::push_back()              // May reallocate
std::mutex / std::lock_guard          // Blocking synchronization
throw / catch                         // Exception handling
std::cout / printf / logging          // I/O operations
File operations                       // Disk I/O
Virtual function calls in tight loops // Indirect calls (where avoidable)
```

**ALWAYS:**
- Pre-allocate buffers in `setupProcessing()` before `setActive(true)`
- Use `std::atomic` with relaxed memory ordering for parameter access
- Use lock-free SPSC ring buffers for thread communication

### 2. VST3 Architecture Separation

Processor and Controller are **separate components**:

```cpp
// Processor (audio thread) - src/processor/
class Processor : public Steinberg::Vst::AudioEffect {
    // Audio processing ONLY
    // NO UI code, NO direct parameter storage for UI
};

// Controller (UI thread) - src/controller/
class Controller : public Steinberg::Vst::EditControllerEx1 {
    // UI and parameter management ONLY
    // NO audio processing code
};
```

**Rules:**
- Never include controller headers in processor files (and vice versa)
- Use `IMessage` for processor↔controller communication
- Processor must work without controller instantiated
- State flows: Host → Processor → Controller (via `setComponentState`)

### 3. Parameter Handling

All parameters at the VST interface boundary are **normalized (0.0 to 1.0)**:

```cpp
// In Controller::initialize()
parameters.addParameter(
    STR16("Gain"),
    STR16("dB"),
    0,                    // stepCount (0 = continuous)
    0.5,                  // defaultValue NORMALIZED
    Steinberg::Vst::ParameterInfo::kCanAutomate,
    kGainId
);

// In Processor::process() - read from atomic
float gain = gain_.load(std::memory_order_relaxed);

// Conversion happens in processParameterChanges()
case kGainId:
    gain_.store(static_cast<float>(normalizedValue * 2.0), // denormalize
               std::memory_order_relaxed);
```

## Code Style

### Modern C++ Requirements

```cpp
// USE: Smart pointers
auto buffer = std::make_unique<float[]>(size);

// USE: RAII
class DelayLine {
    std::vector<float> buffer_;  // Automatically cleaned up
public:
    DelayLine(size_t size) : buffer_(size, 0.0f) {}
};

// USE: constexpr for compile-time computation
constexpr float kPi = 3.14159265358979323846f;
constexpr float dBToLinear(float dB) { return std::pow(10.0f, dB / 20.0f); }

// USE: Move semantics for large buffers
void setBuffer(std::vector<float>&& buffer) {
    buffer_ = std::move(buffer);
}

// AVOID: Raw new/delete
float* buffer = new float[size];  // BAD
delete[] buffer;                   // BAD
```

### DSP Code Guidelines

DSP algorithms go in `src/dsp/` and must be:
1. **Pure functions** - no side effects, testable without VST infrastructure
2. **Header-inline** where performance matters
3. **SIMD-friendly** - contiguous memory access, minimal branching

```cpp
// Good: Pure, testable function
inline void applyGain(float* buffer, size_t numSamples, float gain) noexcept {
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] *= gain;
    }
}

// Good: Aligned for SIMD
alignas(16) std::array<float, 4> simdBuffer;
```

### Naming Conventions

```cpp
// Classes: PascalCase
class AudioProcessor {};

// Functions/Methods: camelCase
void processAudio();

// Member variables: camelCase with trailing underscore
float sampleRate_;
std::atomic<float> gain_;

// Constants: kPascalCase
constexpr float kDefaultGain = 1.0f;

// Namespaces: PascalCase
namespace VSTWork { namespace DSP { ... } }

// Parameter IDs: kDescriptiveNameId
enum ParameterIDs : Steinberg::Vst::ParamID {
    kBypassId = 0,
    kGainId = 1,
};
```

## Layered DSP Architecture

This project uses a **5-layer compositional architecture**. Higher layers compose from lower layers, never the reverse. See [ROADMAP.md](specs/ROADMAP.md) for full details.

```
┌─────────────────────────────────────────────────────────────┐
│                    LAYER 4: USER FEATURES                   │
│  (Tape Mode, BBD Mode, Multi-Tap, Ping-Pong, Shimmer, etc.) │
├─────────────────────────────────────────────────────────────┤
│                  LAYER 3: SYSTEM COMPONENTS                 │
│    (Delay Engine, Modulation Matrix, Feedback Network)      │
├─────────────────────────────────────────────────────────────┤
│                   LAYER 2: DSP PROCESSORS                   │
│  (Filters, Saturation, Pitch Shifter, Diffuser, Envelope)   │
├─────────────────────────────────────────────────────────────┤
│                  LAYER 1: DSP PRIMITIVES                    │
│    (Delay Line, LFO, Biquad, Smoother, Oversampler)         │
├─────────────────────────────────────────────────────────────┤
│                    LAYER 0: CORE UTILITIES                  │
│      (Memory Pool, Lock-free Queue, Fast Math, SIMD)        │
└─────────────────────────────────────────────────────────────┘
```

**Rules:**
- Each layer can ONLY depend on layers below it
- No circular dependencies between layers
- Each layer must be independently testable
- Add layer identifier in file header comments: `// Layer 1: DSP Primitive`

## File Organization

```
src/
├── entry.cpp              # Plugin factory - don't modify unless adding new components
├── plugin_ids.h           # All GUIDs and parameter IDs - single source of truth
├── version.h              # Version info - keep in sync with CMakeLists.txt
├── processor/
│   ├── processor.h        # Audio processor declaration
│   └── processor.cpp      # Audio processing implementation
├── controller/
│   ├── controller.h       # Edit controller declaration
│   └── controller.cpp     # UI/parameter management
└── dsp/
    ├── core/              # Layer 0: Core utilities
    │   ├── memory_pool.h  #   Pre-allocated memory for real-time safety
    │   ├── lock_free_queue.h  # SPSC queue for thread communication
    │   ├── fast_math.h    #   Optimized transcendentals (fastSin, fastTanh, etc.)
    │   └── simd_ops.h     #   SIMD abstractions (SSE/AVX/NEON)
    ├── primitives/        # Layer 1: DSP primitives
    │   ├── delay_line.h   #   Circular buffer with interpolation
    │   ├── lfo.h          #   Low-frequency oscillator (wavetable-based)
    │   ├── biquad.h       #   Biquad filter (Transposed Direct Form II)
    │   ├── smoother.h     #   Parameter smoothing (one-pole, linear ramp)
    │   └── oversampler.h  #   Up/downsampling for nonlinear processing
    ├── processors/        # Layer 2: DSP processors
    │   ├── filter.h       #   Multi-mode filter (LP/HP/BP/Notch/Shelf)
    │   ├── saturator.h    #   Saturation with oversampling
    │   ├── pitch_shifter.h #  Pitch shifting (granular/phase vocoder)
    │   ├── envelope_follower.h # Amplitude tracking
    │   └── diffuser.h     #   Allpass diffusion network
    ├── systems/           # Layer 3: System components
    │   ├── delay_engine.h #   Complete delay with tempo sync
    │   ├── feedback_network.h # Feedback path with filtering/saturation
    │   └── modulation_matrix.h # LFO/envelope routing
    └── features/          # Layer 4: User features
        ├── tape_mode.h    #   Tape delay emulation
        ├── bbd_mode.h     #   Bucket-brigade emulation
        └── shimmer_mode.h #   Pitch-shifted feedback delay
```

## Common Patterns

### Adding a New Parameter

1. **Add ID** in `src/plugin_ids.h`:
```cpp
enum ParameterIDs : Steinberg::Vst::ParamID {
    kBypassId = 0,
    kGainId = 1,
    kNewParamId = 2,  // ADD HERE
    kNumParameters
};
```

2. **Add atomic** in `src/processor/processor.h`:
```cpp
std::atomic<float> newParam_{0.5f};  // Default value
```

3. **Handle in processParameterChanges()** in `processor.cpp`:
```cpp
case kNewParamId:
    newParam_.store(static_cast<float>(value), std::memory_order_relaxed);
    break;
```

4. **Register in Controller::initialize()** in `controller.cpp`:
```cpp
parameters.addParameter(STR16("New Param"), ...);
```

5. **Add to state save/load** in both processor and controller

6. **Add UI control** in `resources/editor.uidesc`

### Adding DSP Processing

1. Create pure function in `src/dsp/`:
```cpp
// src/dsp/my_effect.h
#pragma once
namespace VSTWork::DSP {
    inline void myEffect(float* buffer, size_t numSamples, float param) noexcept {
        // Implementation
    }
}
```

2. Write tests first in `tests/unit/`:
```cpp
TEST_CASE("myEffect processes correctly", "[dsp]") {
    std::array<float, 4> buffer = {1.0f, 0.5f, -0.5f, -1.0f};
    VSTWork::DSP::myEffect(buffer.data(), buffer.size(), 0.5f);
    REQUIRE(buffer[0] == Approx(expected));
}
```

3. Call from `Processor::process()`:
```cpp
#include "dsp/my_effect.h"
// In process():
DSP::myEffect(outputBuffer, numSamples, param_.load(std::memory_order_relaxed));
```

### Pre-allocating Buffers

```cpp
// In Processor::setupProcessing()
Steinberg::tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup) {
    sampleRate_ = setup.sampleRate;
    maxBlockSize_ = setup.maxSamplesPerBlock;

    // ALLOCATE HERE - this is the ONLY safe place
    delayBuffer_.resize(static_cast<size_t>(sampleRate_ * kMaxDelaySeconds));
    std::fill(delayBuffer_.begin(), delayBuffer_.end(), 0.0f);

    return AudioEffect::setupProcessing(setup);
}
```

### Adding a Layer 1 DSP Primitive

1. Create header in `src/dsp/primitives/`:
```cpp
// src/dsp/primitives/my_primitive.h
// Layer 1: DSP Primitive
#pragma once
#include "dsp/core/fast_math.h"  // OK: Layer 0 dependency

namespace VSTWork::DSP {
    class MyPrimitive {
    public:
        void prepare(double sampleRate, size_t maxBlockSize) noexcept;
        void process(float* buffer, size_t numSamples) noexcept;
        void reset() noexcept;
    private:
        // Pre-allocated state only
    };
}
```

2. Write tests in `tests/unit/primitives/`

3. **Never** include headers from Layer 2+ in primitive code

### Adding a Layer 2 DSP Processor

Processors compose primitives. Always check the layer hierarchy before adding includes.

```cpp
// src/dsp/processors/my_processor.h
// Layer 2: DSP Processor
#pragma once
#include "dsp/primitives/biquad.h"      // OK: Layer 1
#include "dsp/primitives/smoother.h"    // OK: Layer 1
#include "dsp/primitives/oversampler.h" // OK: Layer 1
// #include "dsp/systems/delay_engine.h" // FORBIDDEN: Layer 3
```

## DSP Implementation Rules

### Interpolation Selection

Choose interpolation based on use case:

| Use Case | Interpolation | Why |
|----------|---------------|-----|
| Fixed delay in feedback loop | Allpass | No amplitude distortion |
| LFO-modulated delay (chorusing) | Linear or Cubic | Allpass causes artifacts when modulated |
| Pitch shifting | Lagrange or Sinc | Higher quality for transposition |
| Smooth parameter changes | One-pole smoother | Simple and effective |

**Critical:** Never use allpass interpolation for modulated delays!

### Oversampling Requirements

Nonlinear processing (saturation, waveshaping) MUST be oversampled:

```cpp
// In saturator processor
void process(float* buffer, size_t numSamples) noexcept {
    // 1. Upsample (2x minimum)
    oversampler_.upsample(buffer, numSamples, oversampledBuffer_);

    // 2. Apply nonlinearity at higher rate
    for (size_t i = 0; i < numSamples * 2; ++i) {
        oversampledBuffer_[i] = std::tanh(oversampledBuffer_[i] * drive_);
    }

    // 3. Downsample back
    oversampler_.downsample(oversampledBuffer_, numSamples * 2, buffer);

    // 4. DC block after asymmetric saturation
    dcBlocker_.process(buffer, numSamples);
}
```

**2x oversampling** is the practical limit for real-time - higher factors add transient smear that can be more audible than the aliasing they prevent.

### DC Blocking

Always apply DC blocking (~5-20Hz highpass) after:
- Asymmetric saturation (tube, diode curves)
- Rectification
- Any processing that can introduce DC offset

```cpp
// Simple one-pole DC blocker
class DCBlocker {
    float x1_ = 0.0f, y1_ = 0.0f;
    float coeff_ = 0.995f;  // ~15Hz at 44.1kHz
public:
    float process(float x) noexcept {
        float y = x - x1_ + coeff_ * y1_;
        x1_ = x; y1_ = y;
        return y;
    }
};
```

### Feedback Path Safety

Feedback exceeding 100% MUST be limited to prevent runaway oscillation:

```cpp
// In feedback network
float feedbackSample = delayOutput * feedbackAmount_;
if (feedbackAmount_ > 1.0f) {
    // Soft limit feedback signal
    feedbackSample = std::tanh(feedbackSample);
}
```

### Parameter Smoothing Formula

Use one-pole smoothing to prevent zipper noise:

```cpp
// Calculate coefficient once when sample rate changes
// smoothTimeMs = 5-20ms is typical
float smoothCoeff = std::exp(-2.0f * kPi / (smoothTimeMs * 0.001f * sampleRate));

// In process loop
smoothedValue_ = target + smoothCoeff * (smoothedValue_ - target);
```

## Performance Budgets

| Component Type | CPU Target | Memory |
|---------------|------------|--------|
| Layer 1 primitive | < 0.1% per instance | Minimal |
| Layer 2 processor | < 0.5% per instance | Pre-allocated |
| Layer 3 system | < 1% per instance | Fixed buffers |
| Full plugin | < 5% total | 10s delay @ 192kHz max |

- Profile in **Release builds** only
- Test at 44.1kHz stereo for CPU targets
- Maximum delay buffer: 10 seconds at 192kHz = 1,920,000 samples

## Testing Requirements

- All DSP in `src/dsp/` must have corresponding tests in `tests/unit/`
- Tests must pass before committing
- Run tests: `ctest --test-dir build/tests -C Debug --output-on-failure`

```cpp
// Test pattern for DSP functions
TEST_CASE("Function does X", "[dsp][category]") {
    // Arrange
    std::array<float, N> input = {...};
    std::array<float, N> expected = {...};

    // Act
    VSTWork::DSP::function(input.data(), input.size(), params);

    // Assert
    for (size_t i = 0; i < N; ++i) {
        REQUIRE(input[i] == Approx(expected[i]));
    }
}
```

## Build Commands

```bash
# Configure (first time)
cmake --preset windows-x64-debug

# Build
cmake --build --preset windows-x64-debug

# Test
ctest --preset windows-x64-debug

# Release build
cmake --preset windows-x64-release
cmake --build --preset windows-x64-release
```

## VSTGUI Notes

- UI definition: `resources/editor.uidesc` (XML)
- Edit visually: Enable inline editor in debug build, right-click plugin UI
- Custom views: Implement in `Controller::createCustomView()`
- Control tags in uidesc must match `ParameterIDs` values

## Debugging Tips

1. **Audio Issues**: Check for allocations in process() - use memory profiler
2. **Crashes on Load**: Verify GUIDs in plugin_ids.h are unique
3. **Parameters Not Working**: Check ID matches in Controller and uidesc
4. **State Not Saving**: Ensure getState/setState read/write same format in Processor AND Controller handles setComponentState

## Quick Reference: What Goes Where

| Task | File(s) |
|------|---------|
| Add parameter | plugin_ids.h → processor.h → processor.cpp → controller.cpp → editor.uidesc |
| Add Layer 0 utility | src/dsp/core/ → tests/unit/core/ |
| Add Layer 1 primitive | src/dsp/primitives/ → tests/unit/primitives/ |
| Add Layer 2 processor | src/dsp/processors/ → tests/unit/processors/ |
| Add Layer 3 system | src/dsp/systems/ → tests/unit/systems/ |
| Add Layer 4 feature | src/dsp/features/ → tests/unit/features/ → processor.cpp |
| Change UI layout | resources/editor.uidesc |
| Add custom UI control | controller.cpp (createCustomView) |
| Change plugin metadata | plugin_ids.h, version.h, CMakeLists.txt |
| Add new source file | CMakeLists.txt (smtg_add_vst3plugin section) |

### Layer Dependency Quick Check

Before adding an `#include`, verify it respects the layer hierarchy:

| Your File Layer | Can Include |
|-----------------|-------------|
| Layer 0 (core/) | Standard library only |
| Layer 1 (primitives/) | Layer 0 |
| Layer 2 (processors/) | Layer 0, 1 |
| Layer 3 (systems/) | Layer 0, 1, 2 |
| Layer 4 (features/) | Layer 0, 1, 2, 3 |

## References

- [VST3 Developer Portal](https://steinbergmedia.github.io/vst3_dev_portal/)
- [VSTGUI Documentation](https://steinbergmedia.github.io/vst3_doc/vstgui/html/)
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/)
- Project Constitution: `.specify/memory/constitution.md`
- Feature Roadmap: `specs/ROADMAP.md` - layered architecture and implementation phases
