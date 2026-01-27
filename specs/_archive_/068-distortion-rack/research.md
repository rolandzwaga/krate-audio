# Research: DistortionRack System

**Feature**: 068-distortion-rack | **Date**: 2026-01-15

## Research Tasks

This document consolidates research findings for implementing the DistortionRack system.

---

## 1. std::variant with Compile-Time Dispatch Pattern

### Context

The spec requires using `std::variant` with compile-time dispatch via template visitor for zero-overhead processor abstraction. This avoids virtual function call overhead while allowing runtime processor type selection.

### Decision

**Use `std::visit` with generic lambda visitor for processing.**

### Rationale

1. **Zero-overhead abstraction**: `std::visit` with a small variant (7 processor types + monostate) compiles to a jump table, equivalent to a switch statement.

2. **Compile-time dispatch**: The visitor pattern resolves which processor method to call at compile time based on the held type.

3. **No virtual calls**: Unlike an interface-based approach, `std::variant` stores processors inline and dispatches without vtable lookup.

4. **Type safety**: The compiler ensures all variant alternatives are handled by the visitor.

### Implementation Pattern

```cpp
// Variant definition with all processor types plus monostate for empty
using ProcessorVariant = std::variant<
    std::monostate,          // Empty/bypass slot
    Waveshaper,              // Layer 1 primitive
    TubeStage,               // Layer 2 processor
    DiodeClipper,            // Layer 2 processor
    WavefolderProcessor,     // Layer 2 processor
    TapeSaturator,           // Layer 2 processor
    FuzzProcessor,           // Layer 2 processor
    BitcrusherProcessor      // Layer 2 processor
>;

// Processing visitor - called via std::visit
struct ProcessVisitor {
    float* buffer;
    size_t numSamples;

    void operator()(std::monostate&) const noexcept {
        // Bypass - do nothing
    }

    void operator()(Waveshaper& ws) const noexcept {
        ws.processBlock(buffer, numSamples);
    }

    // Generic fallback for Layer 2 processors (all have same signature)
    template<typename T>
    void operator()(T& processor) const noexcept {
        processor.process(buffer, numSamples);
    }
};

// Usage in process loop
std::visit(ProcessVisitor{bufferPtr, numSamples}, slot.processor);
```

### Alternatives Considered

| Alternative | Why Rejected |
|-------------|--------------|
| Virtual interface (IProcessor) | Virtual call overhead per sample; need heap allocation |
| if-else chain with type ID | Runtime branching; error-prone if new types added |
| Template-based slot (compile-time fixed) | Cannot change slot type at runtime |
| std::function wrapper | Type erasure overhead; heap allocation possible |

---

## 2. Stereo Processing with Mono Processors

### Context

All Layer 2 distortion processors (TubeStage, DiodeClipper, etc.) are mono-only with `void process(float* buffer, size_t numSamples)` signature. The DistortionRack needs stereo processing.

### Decision

**Instantiate TWO processors per slot - one for L channel, one for R channel.**

### Rationale

1. **Simple and predictable**: Each channel processes independently with identical parameters.

2. **Matches existing processor design**: Layer 2 processors are designed as mono for maximum flexibility.

3. **No cross-channel artifacts**: Independent processing avoids unintended stereo interaction.

4. **Memory efficient**: Two mono processors use less memory than one stereo processor would (no interleaved buffers).

### Implementation Pattern

```cpp
struct Slot {
    // Two processor instances - one per channel
    ProcessorVariant processorL;
    ProcessorVariant processorR;

    // Per-slot DC blockers
    DCBlocker dcBlockerL;
    DCBlocker dcBlockerR;

    // Per-slot control smoothers
    OnePoleSmoother enableSmoother;
    OnePoleSmoother mixSmoother;
    OnePoleSmoother gainSmoother;

    // Slot state
    bool enabled = false;
    float mix = 1.0f;      // 0.0 = dry, 1.0 = wet
    float gainDb = 0.0f;   // -24 to +24 dB
    SlotType type = SlotType::Empty;
};
```

### Alternatives Considered

| Alternative | Why Rejected |
|-------------|--------------|
| Single processor with stereo API | Would require modifying all Layer 2 processors |
| Interleaved stereo buffer | Adds complexity; processors expect mono buffers |
| Linked stereo (process L, copy to R) | Breaks stereo image; not true stereo processing |

---

## 3. Oversampling Factor Management

### Context

The spec requires global oversampling (1x/2x/4x) around the entire chain. The Oversampler template only supports 2x and 4x factors. For 1x (no oversampling), the Oversampler should be bypassed entirely.

### Decision

**Store oversampling factor as int (1/2/4) and conditionally use Oversampler templates.**

### Rationale

1. **Template instantiation**: Must instantiate both `Oversampler<2, 2>` and `Oversampler<4, 2>` at compile time since factor is runtime-selectable.

2. **1x bypass**: For factor=1, skip oversampling entirely - process at base rate.

3. **Sample rate adjustment**: When oversampling, prepare processors at oversampledRate = baseRate * factor.

### Implementation Pattern

```cpp
class DistortionRack {
private:
    // Oversampler instances (both always exist, one is used based on factor_)
    Oversampler<2, 2> oversampler2x_;
    Oversampler<4, 2> oversampler4x_;
    int oversamplingFactor_ = 1;  // 1, 2, or 4

public:
    void setOversamplingFactor(int factor) noexcept {
        // Only accept valid factors
        if (factor == 1 || factor == 2 || factor == 4) {
            oversamplingFactor_ = factor;
            // Note: Full effect requires re-calling prepare()
        }
    }

    void process(float* left, float* right, size_t numSamples) noexcept {
        if (oversamplingFactor_ == 1) {
            // Direct processing - no oversampling
            processChain(left, right, numSamples);
        } else if (oversamplingFactor_ == 2) {
            oversampler2x_.process(left, right, numSamples,
                [this](float* l, float* r, size_t n) {
                    processChain(l, r, n);
                });
        } else {  // factor == 4
            oversampler4x_.process(left, right, numSamples,
                [this](float* l, float* r, size_t n) {
                    processChain(l, r, n);
                });
        }
    }
};
```

### Alternatives Considered

| Alternative | Why Rejected |
|-------------|--------------|
| Single Oversampler with runtime factor | Oversampler is a template; factor must be compile-time |
| std::variant for oversamplers | Adds complexity; we need both anyway |
| Dynamic allocation based on factor | Violates RT-safety in setOversamplingFactor() |

---

## 4. Per-Slot DC Blocking Strategy

### Context

Per-slot DC blocking is required after each enabled slot (FR-048). DC blocking prevents DC offset accumulation from asymmetric saturation.

### Decision

**Apply DCBlocker after each enabled slot processes, before mix blend.**

### Rationale

1. **DC offset source**: Asymmetric saturation (bias, asymmetric clipping) generates DC.

2. **Per-slot is safer**: Blocking per-slot prevents DC accumulation through the chain.

3. **After saturation**: DC blocker should run on the processed (wet) signal before mix blending.

4. **10Hz cutoff**: Standard cutoff that removes DC without affecting audible bass.

### Signal Flow

```
Input -> [Slot 0 Processor] -> [DC Blocker] -> [Mix with Dry] -> [Gain] ->
      -> [Slot 1 Processor] -> [DC Blocker] -> [Mix with Dry] -> [Gain] ->
      -> [Slot 2 Processor] -> [DC Blocker] -> [Mix with Dry] -> [Gain] ->
      -> [Slot 3 Processor] -> [DC Blocker] -> [Mix with Dry] -> [Gain] ->
      -> Output
```

### Alternatives Considered

| Alternative | Why Rejected |
|-------------|--------------|
| Single DC blocker at output | DC could accumulate through chain causing saturation drift |
| DC blocker before processor | Removes DC generated by previous slot but not current |
| Optional DC blocking | Could cause issues with certain saturation chains |

---

## 5. Memory Allocation Strategy

### Context

The spec requires immediate allocation during setSlotType() calls (FR-003a), not deferred to process(). This ensures RT-safety in process().

### Decision

**All processor allocation happens in setSlotType() on the control thread.**

### Rationale

1. **RT-safety**: process() must never allocate. By allocating in setSlotType(), the audio thread only accesses pre-allocated memory.

2. **std::variant storage**: std::variant stores processors inline (no heap allocation for the processors themselves).

3. **Processor internal allocation**: Layer 2 processors may allocate in their prepare() method. Call prepare() in setSlotType() immediately after creating the processor.

### Implementation Pattern

```cpp
void setSlotType(size_t slotIndex, SlotType type) noexcept {
    if (slotIndex >= kNumSlots) return;
    auto& slot = slots_[slotIndex];

    // Create processor based on type
    switch (type) {
        case SlotType::Empty:
            slot.processorL = std::monostate{};
            slot.processorR = std::monostate{};
            break;
        case SlotType::TubeStage: {
            slot.processorL = TubeStage{};
            slot.processorR = TubeStage{};
            // Prepare immediately with stored sample rate/block size
            auto& procL = std::get<TubeStage>(slot.processorL);
            auto& procR = std::get<TubeStage>(slot.processorR);
            procL.prepare(preparedSampleRate_, preparedMaxBlockSize_);
            procR.prepare(preparedSampleRate_, preparedMaxBlockSize_);
            break;
        }
        // ... other types
    }

    slot.type = type;
}
```

### Thread Safety Note

setSlotType() modifies the variant which could race with process(). For full thread safety, the caller should ensure setSlotType() is not called while process() is running. In practice, this is handled at the VST3 Processor level using appropriate synchronization.

---

## 6. Parameter Smoothing Configuration

### Context

The spec requires 5ms smoothing on enable, mix, and gain parameters (FR-009, FR-015, FR-046).

### Decision

**Use OnePoleSmoother with 5ms smoothing time for all per-slot controls.**

### Rationale

1. **5ms is standard**: Sufficient to avoid clicks while remaining responsive.

2. **Per-parameter smoothing**: Each control (enable, mix, gain) has its own smoother for independent transitions.

3. **OnePoleSmoother API**: Configure once in prepare(), set target on parameter change, call process() per sample.

### Implementation Pattern

```cpp
// In prepare():
for (auto& slot : slots_) {
    slot.enableSmoother.configure(5.0f, static_cast<float>(sampleRate));
    slot.mixSmoother.configure(5.0f, static_cast<float>(sampleRate));
    slot.gainSmoother.configure(5.0f, static_cast<float>(sampleRate));

    // Initialize to current values
    slot.enableSmoother.snapTo(slot.enabled ? 1.0f : 0.0f);
    slot.mixSmoother.snapTo(slot.mix);
    slot.gainSmoother.snapTo(dbToGain(slot.gainDb));
}

// In setSlotEnabled():
slots_[index].enableSmoother.setTarget(enabled ? 1.0f : 0.0f);

// In process() per sample:
float enableGain = slot.enableSmoother.process();
float mix = slot.mixSmoother.process();
float gain = slot.gainSmoother.process();

// Apply: output = dry * (1 - enableGain * mix) + wet * enableGain * mix * gain
```

---

## Summary of Decisions

| Research Area | Decision | Rationale |
|---------------|----------|-----------|
| Processor wrapper | std::variant with std::visit | Zero-overhead, type-safe |
| Stereo processing | Two mono processors per slot | Matches Layer 2 design |
| Oversampling | Conditional template instantiation | RT-safe factor switching |
| DC blocking | Per-slot after processing | Prevents DC accumulation |
| Memory allocation | Immediate in setSlotType() | RT-safe process() |
| Parameter smoothing | OnePoleSmoother @ 5ms | Click-free transitions |
